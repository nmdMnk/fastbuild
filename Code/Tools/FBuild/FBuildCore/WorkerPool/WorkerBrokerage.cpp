// WorkerBrokerage - Manage worker discovery
//------------------------------------------------------------------------------

// Includes
//------------------------------------------------------------------------------
#include "WorkerBrokerage.h"

// FBuild
#include "Tools/FBuild/FBuildCore/Protocol/Protocol.h"
#include "Tools/FBuild/FBuildCore/FBuildVersion.h"
#include "Tools/FBuild/FBuildCore/FLog.h"
#include "Tools/FBuild/FBuildWorker/Worker/WorkerSettings.h"
#include "Tools/FBuild/FBuildCore/WorkerPool/WorkerConnectionPool.h"

// Core
#include "Core/Env/Env.h"
#include "Core/FileIO/FileIO.h"
#include "Core/FileIO/FileStream.h"
#include "Core/FileIO/PathUtils.h"
#include "Core/Network/Network.h"
#include "Core/Network/TCPConnectionPool.h"
#include "Core/Profile/Profile.h"
#include "Core/Strings/AStackString.h"
#include "Core/Process/Thread.h"
#include "Core/Time/Time.h"
#include "Core/Tracing/Tracing.h"

#if defined( __APPLE__ )

#include <sys/socket.h>
#include <ifaddrs.h>
#include <string.h>
#include <netinet/in.h>
#include <arpa/inet.h>

static bool ConvertHostNameToLocalIP4( AString& hostName )
{
    bool result = false;

    struct ifaddrs * allIfAddrs;
    if ( getifaddrs( &allIfAddrs ) == 0 )
    {
        struct ifaddrs * addr = allIfAddrs;
        char ipString[48] = { 0 };
        while ( addr )
        {
            if ( addr->ifa_addr )
            {
                if ( addr->ifa_addr->sa_family == AF_INET && strcmp( addr->ifa_name, "en0" ) == 0 )
                {
                    struct sockaddr_in * sockaddr = ( struct sockaddr_in * ) addr->ifa_addr;
                    inet_ntop( AF_INET, &sockaddr->sin_addr, ipString, sizeof( ipString ) );
                    hostName = ipString;
                    result = true;
                    break;
                }
            }
            addr = addr->ifa_next;
        }

        freeifaddrs( allIfAddrs );
    }

    return result;
}

#endif // __APPLE__

// CONSTRUCTOR
//------------------------------------------------------------------------------
WorkerBrokerage::WorkerBrokerage()
    : m_BrokerageInitialized( false )
    , m_PreferHostName( false )
    , m_ConnectionPool( nullptr )
    , m_Connection( nullptr )
    , m_WorkerListUpdateReady( false )
{
}

// InitBrokerage
//------------------------------------------------------------------------------
void WorkerBrokerage::InitBrokerage()
{
    PROFILE_FUNCTION;

    if ( m_BrokerageInitialized )
    {
        return;
    }

    if ( m_CoordinatorAddress.IsEmpty() == true )
    {
        AStackString<> coordinator;
        if ( Env::GetEnvVariable( "FASTBUILD_COORDINATOR", coordinator ) )
        {
            m_CoordinatorAddress = coordinator;
        }
    }

    if ( m_CoordinatorAddress.IsEmpty() == true )
    {
        OUTPUT( "Using brokerage folder\n" );

        // brokerage path includes version to reduce unnecssary comms attempts
        const uint32_t protocolVersion = Protocol::PROTOCOL_VERSION_MAJOR;

        // root folder
        AStackString<> brokeragePath;
        if ( Env::GetEnvVariable( "FASTBUILD_BROKERAGE_PATH", brokeragePath ) )
        {
            // FASTBUILD_BROKERAGE_PATH can contain multiple paths separated by semi-colon. The worker will register itself into the first path only but
            // the additional paths are paths to additional broker roots allowed for finding remote workers (in order of priority)
            const char * start = brokeragePath.Get();
            const char * end = brokeragePath.GetEnd();
            AStackString<> pathSeparator( ";" );
            while ( true )
            {
                AStackString<> root;
                AStackString<> brokerageRoot;

                const char * separator = brokeragePath.Find( pathSeparator, start, end );
                if ( separator != nullptr )
                {
                    root.Append( start, (size_t)( separator - start ) );
                }
                else
                {
                    root.Append( start, (size_t)( end - start ) );
                }
                root.TrimStart( ' ' );
                root.TrimEnd( ' ' );
                // <path>/<group>/<version>/
                #if defined( __WINDOWS__ )
                    brokerageRoot.Format( "%s\\main\\%u.windows\\", root.Get(), protocolVersion );
                #elif defined( __OSX__ )
                    brokerageRoot.Format( "%s/main/%u.osx/", root.Get(), protocolVersion );
                #else
                    brokerageRoot.Format( "%s/main/%u.linux/", root.Get(), protocolVersion );
                #endif

                m_BrokerageRoots.Append( brokerageRoot );
                if ( !m_BrokerageRootPaths.IsEmpty() )
                {
                    m_BrokerageRootPaths.Append( pathSeparator );
                }

                m_BrokerageRootPaths.Append( brokerageRoot );

                if ( separator != nullptr )
                {
                    start = separator + 1;
                }
                else
                {
                    break;
                }
            }
        }
    }
    else
    {
        OUTPUT( "Using coordinator\n" );
    }

    UpdateBrokerageFilePath();
    m_BrokerageInitialized = true;
}

// DESTRUCTOR
//------------------------------------------------------------------------------
WorkerBrokerage::~WorkerBrokerage() = default;

// FindWorkers
//------------------------------------------------------------------------------
void WorkerBrokerage::FindWorkers( Array< AString > & workerList )
{
    PROFILE_FUNCTION;

    // Check for workers for the FASTBUILD_WORKERS environment variable
    // which is a list of worker addresses separated by a semi-colon.
    AStackString<> workersEnv;
    if ( Env::GetEnvVariable( "FASTBUILD_WORKERS", workersEnv ) )
    {
        // If we find a valid list of workers, we'll use that
        workersEnv.Tokenize( workerList, ';' );
        if ( workerList.IsEmpty() == false )
        {
            return;
        }
    }

    // check for workers through brokerage

    // Init the brokerage
    InitBrokerage();
    if ( m_BrokerageRoots.IsEmpty() && m_CoordinatorAddress.IsEmpty() )
    {
        FLOG_WARN( "No brokerage root and no coordinator available; did you set FASTBUILD_BROKERAGE_PATH or launched with -coordinator param?" );
        return;
    }
    
    // Get addresses for the local host
    StackArray<AString> localAddresses;
    Network::GetIPv4Addresses( localAddresses );

    if ( ConnectToCoordinator() )
    {
        m_WorkerListUpdateReady = false;


        OUTPUT( "Requesting worker list\n");

        const Protocol::MsgRequestWorkerList msg(false);
        msg.Send( m_Connection );


        while ( m_WorkerListUpdateReady == false )
        {
            Thread::Sleep( 1 );
        }

        DisconnectFromCoordinator();

        OUTPUT( "Worker list received: %u workers\n", (uint32_t)m_WorkerListUpdate.GetSize() );
        if ( m_WorkerListUpdate.GetSize() == 0 )
        {
            FLOG_WARN( "No workers received from coordinator" );
            return; // no files found
        }

        // presize
        if ( ( workerList.GetSize() + m_WorkerListUpdate.GetSize() ) > workerList.GetCapacity() )
        {
            workerList.SetCapacity( workerList.GetSize() + m_WorkerListUpdate.GetSize() );
        }

        // convert worker strings
        const uint32_t* const end = m_WorkerListUpdate.End();
        for ( uint32_t* it = m_WorkerListUpdate.Begin(); it != end; ++it )
        {
            AStackString<> workerName;
            TCPConnectionPool::GetAddressAsString( *it, workerName );
            
            // Filter out local addresses
            if ( localAddresses.Find( workerName ) )
            {
                continue;
            }
            
            if ( workerName.CompareI( m_HostName ) != 0 && workerName.CompareI( "127.0.0.1" ) )
            {
                workerList.Append( workerName );
            }
            else
            {
                OUTPUT( "Skipping woker %s\n", workerName.Get() );
            }
        }

        m_WorkerListUpdate.Clear();
    }
    else if ( !m_BrokerageRoots.IsEmpty() )
    {
        Array< AString > results( 256 );
        for( AString& root : m_BrokerageRoots )
        {
            const size_t filesBeforeSearch = results.GetSize();
            if ( !FileIO::GetFiles( root,
                                    AStackString<>( "*" ),
                                    false,
                                    &results ) )
            {
                FLOG_WARN( "No workers found in '%s'", root.Get() );
            }
            else
            {
                FLOG_WARN( "%zu workers found in '%s'", results.GetSize() - filesBeforeSearch, root.Get() );
            }
        }

        // presize
        if ( ( workerList.GetSize() + results.GetSize() ) > workerList.GetCapacity() )
        {
            workerList.SetCapacity( workerList.GetSize() + results.GetSize() );
        }

        // convert worker strings
        const AString * const end = results.End();
        for ( AString * it = results.Begin(); it != end; ++it )
        {
            const AString & fileName = *it;
            const char * lastSlash = fileName.FindLast( NATIVE_SLASH );
            AStackString<> workerName( lastSlash + 1 );
            
            // Filter out local addresses
            if ( localAddresses.Find( workerName ) )
            {
                continue;
            }

            if ( workerName.CompareI( m_HostName ) != 0 )
            {
                workerList.Append( workerName );
            }
        }
    }
}

// UpdateWorkerList
//------------------------------------------------------------------------------
void WorkerBrokerage::UpdateWorkerList( Array<uint32_t>& workerListUpdate )
{
    m_WorkerListUpdate.Swap( workerListUpdate );
    m_WorkerListUpdateReady = true;
}
//------------------------------------------------------------------------------
// UpdateBrokerageFilePath
//------------------------------------------------------------------------------
void WorkerBrokerage::UpdateBrokerageFilePath()
{
    if ( !m_BrokerageRoots.IsEmpty() )
    {
        if ( !m_IPAddress.IsEmpty() && !m_PreferHostName )
        {
            m_BrokerageFilePath.Format( "%s%s", m_BrokerageRoots[0].Get(), m_IPAddress.Get() );
        }
        else
        {
            m_BrokerageFilePath.Format( "%s%s", m_BrokerageRoots[0].Get(), m_HostName.Get() );
        }
    }
}

// ConnectToCoordinator
//------------------------------------------------------------------------------
bool WorkerBrokerage::ConnectToCoordinator()
{
    if ( m_CoordinatorAddress.IsEmpty() == false )
    {
        m_ConnectionPool = FNEW( WorkerConnectionPool );
        m_Connection = m_ConnectionPool->Connect( m_CoordinatorAddress, Protocol::COORDINATOR_PORT, 2000, this ); // 2000ms connection timeout
        if ( m_Connection == nullptr )
        {
            OUTPUT( "Failed to connect to the coordinator at %s\n", m_CoordinatorAddress.Get() );
            FDELETE m_ConnectionPool;
            m_ConnectionPool = nullptr;
            // m_CoordinatorAddress.Clear();
            return false;
        }

        OUTPUT( "Connected to the coordinator\n" );
        return true;
    }

    return false;
}

// DisconnectFromCoordinator
//------------------------------------------------------------------------------
void WorkerBrokerage::DisconnectFromCoordinator()
{
    if ( m_ConnectionPool )
    {
        FDELETE m_ConnectionPool;
        m_ConnectionPool = nullptr;
        m_Connection = nullptr;

        OUTPUT( "Disconnected from the coordinator\n" );
    }
}

//------------------------------------------------------------------------------

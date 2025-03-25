// WorkerBrokerageServer - Server-side worker registration
//------------------------------------------------------------------------------

// Includes
//------------------------------------------------------------------------------
#include "WorkerBrokerageServer.h"

// FBuildCore
#include "Tools/FBuild/FBuildCore/FBuildVersion.h"
#include "Tools/FBuild/FBuildCore/FLog.h"
#include "Tools/FBuild/FBuildWorker/Worker/WorkerSettings.h"
#include "Tools/FBuild/FBuildCore/Protocol/Protocol.h"

// Core
#include "Core/Env/Env.h"
#include "Core/FileIO/FileIO.h"
#include "Core/FileIO/MemoryStream.h"
#include "Core/Network/Network.h"
#include "Core/Network/TCPConnectionPool.h"
#include "Core/Strings/AStackString.h"
#include "Core/Time/Time.h"

// Static Data
//------------------------------------------------------------------------------
static const float sBrokerageElapsedTimeBetweenClean = ( 12 * 60 * 60.0f );
static const uint32_t sBrokerageCleanOlderThan = ( 24 * 60 * 60 );
static const float sBrokerageAvailabilityUpdateTime = ( 10.0f );
static const float sBrokerageIPAddressUpdateTime = ( 5 * 60.0f );

// CONSTRUCTOR
//------------------------------------------------------------------------------
WorkerBrokerageServer::WorkerBrokerageServer( bool preferHostname)
{
    m_PreferHostName = preferHostname;
    m_TimerLastUpdate.Start();
    m_TimerLastIPUpdate.Start();
    m_TimerLastCleanBroker.Start( sBrokerageElapsedTimeBetweenClean ); // Set timer so we trigger right away
}

// DESTRUCTOR
//------------------------------------------------------------------------------
WorkerBrokerageServer::~WorkerBrokerageServer()
{
    // Make a best-effort attempt to remove our token
    if ( m_Available )
    {
        FileIO::FileDelete( m_BrokerageFilePath.Get() );
    }
}

// SetAvailability
//------------------------------------------------------------------------------
void WorkerBrokerageServer::SetAvailability( bool available )
{
    // Init the brokerage if not already
    InitBrokerage();

    // ignore if brokerage not configured
    if ( m_BrokerageRoots.IsEmpty() && m_CoordinatorAddress.IsEmpty() )
    {
        return;
    }

    if ( available )
    {
        // Check the last update time to avoid too much File IO.
        const float elapsedTime = m_TimerLastUpdate.GetElapsed();
        if ( elapsedTime >= sBrokerageAvailabilityUpdateTime )
        {
            // If settings have changed, update worker config 
            // If settings have not changed, update the modification timestamp
            const WorkerSettings & workerSettings = WorkerSettings::Get();
            const uint64_t settingsWriteTime = workerSettings.GetSettingsWriteTime();
            bool updateWorkerConfig = ( settingsWriteTime > m_SettingsWriteTime );
            bool hostNameOrIPChanged = false;
            
            // Check IP last update time and determine if host name or IP address has changed
            if ( m_HostName.IsEmpty() ||
                 m_IPAddress.IsEmpty() ||
                 ( m_TimerLastIPUpdate.GetElapsed() >= sBrokerageIPAddressUpdateTime ) )
            {
                AStackString<> hostName;
                AStackString<> domainName;
                AStackString<> ipAddress;

                // Get host and domain name as FQDN could have changed
                Network::GetHostName( hostName );
                Network::GetDomainName( domainName );

                // Resolve host name to ip address
                const uint32_t ip = Network::GetHostIPFromName( hostName );
                if ( ( ip != 0 ) && ( ip != 0x0100007f ) )
                {
                    TCPConnectionPool::GetAddressAsString( ip, ipAddress );
                }

                if ( ( hostName != m_HostName ) || ( domainName != m_DomainName ) || ( ipAddress != m_IPAddress ) )
                {
                    m_HostName = hostName;
                    m_DomainName = domainName;
                    m_IPAddress = ipAddress;
                    
                    hostNameOrIPChanged = true;

                    // Host name, domain name, or IP address changed - force update worker config
                    updateWorkerConfig = true;
                }

                // Restart the IP timer
                m_TimerLastIPUpdate.Start();
            }

            if ( ConnectToCoordinator() )
            {
                const Protocol::MsgSetWorkerStatus msgSetWorkerStatus( available );
                msgSetWorkerStatus.Send( m_Connection );

                if (updateWorkerConfig)
                {
                    // Username
                    AStackString<> userName;
                    Env::GetLocalUserName( userName );
                    
                    // Mode
                    AStackString<> mode;
                    switch ( workerSettings.GetMode() )
                    {
                        case WorkerSettings::DISABLED:      mode = "disabled";      break;
                        case WorkerSettings::WHEN_IDLE:     mode.Format( "idle @ %u%%", workerSettings.GetIdleThresholdPercent() ); break;
                        case WorkerSettings::DEDICATED:     mode = "dedicated";    break;
                        case WorkerSettings::PROPORTIONAL:  mode = "proportional"; break;
                    }
                    
                    MemoryStream ms;
                    ms.Write(AString(FBUILD_VERSION_STRING));
                    ms.Write(userName);
                    ms.Write(m_HostName);
                    ms.Write(m_DomainName);
                    ms.Write(mode);
                    ms.Write(workerSettings.GetNumCPUsToUse());
                    ms.Write(Env::GetNumProcessors());
                    ms.Write(workerSettings.GetMinimumFreeMemoryMiB());
                    
                    const Protocol::MsgUpdateWorkerInfo msgUpdateWorkerInfo;
                    msgUpdateWorkerInfo.Send(m_Connection, ms);
                }
                
                DisconnectFromCoordinator();
            }
            else
            {
                if (hostNameOrIPChanged)
                {
                    // Remove existing brokerage file, as filename is being updated
                    FileIO::FileDelete( m_BrokerageFilePath.Get() );

                    // Update brokerage path
                    UpdateBrokerageFilePath();
                }
                
                if ( updateWorkerConfig == false )
                {
                    // Update the modified time
                    // (Allows an external process to delete orphaned files (from crashes/terminated workers)
                    if ( FileIO::SetFileLastWriteTimeToNow( m_BrokerageFilePath ) == false )
                    {
                        // Failed to update time - try to create or recreate the file
                        updateWorkerConfig = true;
                    }
                }
                if ( updateWorkerConfig )
                {
                    // Version
                    AStackString<> buffer;
                    buffer.AppendFormat( "Version: %s\n", FBUILD_VERSION_STRING );

                    // Username
                    AStackString<> userName;
                    Env::GetLocalUserName( userName );
                    buffer.AppendFormat( "User: %s\n", userName.Get() );

                    // Host Name
                    buffer.AppendFormat( "Host Name: %s\n", m_HostName.Get() );

                    if ( !m_DomainName.IsEmpty() )
                    {
                        // Domain Name
                        buffer.AppendFormat( "Domain Name: %s\n", m_DomainName.Get() );

                        // Fully Quantified Domain Name
                        buffer.AppendFormat( "FQDN: %s.%s\n", m_HostName.Get(), m_DomainName.Get() );
                    }

                    // IP Address
                    buffer.AppendFormat( "IPv4 Address: %s\n", m_IPAddress.Get() );

                    // CPU Thresholds
                    buffer.AppendFormat( "CPUs: %u/%u\n", workerSettings.GetNumCPUsToUse(), Env::GetNumProcessors() );

                    // Memory Threshold
                    buffer.AppendFormat( "Memory: %u\n", workerSettings.GetMinimumFreeMemoryMiB() );

                    // Mode
                    switch ( workerSettings.GetMode() )
                    {
                        case WorkerSettings::DISABLED:      buffer += "Mode: disabled\n";     break;
                        case WorkerSettings::WHEN_IDLE:     buffer.AppendFormat( "Mode: idle @ %u%%\n", workerSettings.GetIdleThresholdPercent() ); break;
                        case WorkerSettings::DEDICATED:     buffer += "Mode: dedicated\n";    break;
                        case WorkerSettings::PROPORTIONAL:  buffer += "Mode: proportional\n"; break;
                    }

                    if (!m_BrokerageRoots.IsEmpty())
                    {
                        // Create/write file which signifies availability
                        FileIO::EnsurePathExists(m_BrokerageRoots[0]);
                        FileStream fs;
                        if (fs.Open(m_BrokerageFilePath.Get(), FileStream::WRITE_ONLY))
                        {
                            fs.WriteBuffer(buffer.Get(), buffer.GetLength());

                            // Take note of time we wrote the settings
                            m_SettingsWriteTime = settingsWriteTime;
                        }
                    }
                }
            }

            // Restart the timer
            m_TimerLastUpdate.Start();
        }
    }
    else if ( m_Available != available )
    {
        if ( ConnectToCoordinator() )
        {
            const Protocol::MsgSetWorkerStatus msg( available );
            msg.Send( m_Connection );
            DisconnectFromCoordinator();
        }
        else
        {
            // remove file to remove availability
            FileIO::FileDelete( m_BrokerageFilePath.Get() );

            // Restart the timer
            m_TimerLastUpdate.Start();
        }
    }
    m_Available = available;

    // Handle brokerage cleaning
    if ( m_CoordinatorAddress.IsEmpty() && m_TimerLastCleanBroker.GetElapsed() >= sBrokerageElapsedTimeBetweenClean )
    {
        const uint64_t fileTimeNow = Time::FileTimeToSeconds( Time::GetCurrentFileTime() );

        Array< AString > files( 256 );
        if ( !FileIO::GetFiles( m_BrokerageRoots[ 0 ],
                                AStackString<>( "*" ),
                                false,
                                &files ) )
        {
            FLOG_WARN( "No workers found in '%s' (or inaccessible)", m_BrokerageRoots[ 0 ].Get() );
        }

        for ( const AString & file : files )
        {
            const uint64_t lastWriteTime = Time::FileTimeToSeconds( FileIO::GetFileLastWriteTime( file ) );
            if ( ( fileTimeNow > lastWriteTime ) && ( ( fileTimeNow - lastWriteTime ) > sBrokerageCleanOlderThan ) )
            {
                FLOG_WARN( "Removing '%s' (too old)", file.Get() );
                FileIO::FileDelete( file.Get() );
            }
        }

        // Restart the timer
        m_TimerLastCleanBroker.Start();
    }
}

//------------------------------------------------------------------------------

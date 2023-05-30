// WorkerConnectionPool
//------------------------------------------------------------------------------

// Includes
//------------------------------------------------------------------------------
#include "WorkerConnectionPool.h"
#include "WorkerBrokerage.h"

// FBuild
#include "Tools/FBuild/FBuildCore/Protocol/Protocol.h"

// Core
#include "Core/Strings/AStackString.h"
#include "Core/Tracing/Tracing.h"
#include "Core/FileIO/ConstMemoryStream.h"
#include "Core/FileIO/MemoryStream.h"

// CONSTRUCTOR
//------------------------------------------------------------------------------
WorkerConnectionPool::WorkerConnectionPool()
    : TCPConnectionPool()
    , m_CurrentMessage( nullptr )
{
}

// DESTRUCTOR
//------------------------------------------------------------------------------
WorkerConnectionPool::~WorkerConnectionPool()
{
    ShutdownAllConnections();
}

// OnReceive
//------------------------------------------------------------------------------
void WorkerConnectionPool::OnReceive( const ConnectionInfo * connection, void * data, uint32_t size, bool & keepMemory )
{
    keepMemory = true; // we'll take care of freeing the memory

    // are we expecting a msg, or the payload for a msg?
    void * payload = nullptr;
    size_t payloadSize = 0;
    if ( m_CurrentMessage == nullptr )
    {
        // message
        m_CurrentMessage = static_cast< const Protocol::IMessage * >( data );
        if ( m_CurrentMessage->HasPayload() )
        {
            return;
        }
    }
    else
    {
        // payload
        ASSERT( m_CurrentMessage->HasPayload() );
        payload = data;
        payloadSize = size;
    }

    const Protocol::IMessage * imsg = m_CurrentMessage;
    const Protocol::MessageType messageType = imsg->GetType();

    PROTOCOL_DEBUG( "Coordinator : %u (%s)\n", messageType, GetProtocolMessageDebugName( messageType ) );

    switch ( messageType )
    {
        case Protocol::MSG_REQUEST_WORKER_LIST:
        {
            const Protocol::MsgRequestWorkerList * msg = static_cast< const Protocol::MsgRequestWorkerList * >( imsg );
            Process( connection, msg );
            break;
        }
        case Protocol::MSG_WORKER_LIST:
        {
            const Protocol::MsgWorkerList * msg = static_cast< const Protocol::MsgWorkerList * >( imsg );
            Process( connection, msg, payload, payloadSize );
            break;
        }
        case Protocol::MSG_SET_WORKER_STATUS:
        {
            const Protocol::MsgSetWorkerStatus * msg = static_cast< const Protocol::MsgSetWorkerStatus * >( imsg );
            Process( connection, msg );
            break;
        }
        case Protocol::MSG_UPDATE_WORKER_INFO:
        {
            const Protocol::MsgUpdateWorkerInfo * msg = static_cast< const Protocol::MsgUpdateWorkerInfo * >( imsg );
            Process( connection, msg, payload, payloadSize );
            break;
        }
        default:
        {
            // unknown message type
            ASSERT( false ); // this indicates a protocol bug
            // AStackString<> remoteAddr;
            // TCPConnectionPool::GetAddressAsString( connection->GetRemoteAddress(), remoteAddr );
            // DIST_INFO( "Protocol Error: %s\n", remoteAddr.Get() );
            Disconnect( connection );
            break;
        }
    }

    // free everything
    FREE( (void *)( m_CurrentMessage ) );
    FREE( payload );
    m_CurrentMessage = nullptr;
}

// OnConnected
//------------------------------------------------------------------------------
void WorkerConnectionPool::OnConnected( const ConnectionInfo * connection )
{
    AStackString<> remoteAddr;
    TCPConnectionPool::GetAddressAsString( connection->GetRemoteAddress(), remoteAddr );
    OUTPUT( "OnConnected %s\n", remoteAddr.Get() );
}

// OnDisconnected
//------------------------------------------------------------------------------
void WorkerConnectionPool::OnDisconnected( const ConnectionInfo * )
{
}

// Process ( MsgRequestWorkerList )
//------------------------------------------------------------------------------
void WorkerConnectionPool::Process( const ConnectionInfo * connection, const Protocol::MsgRequestWorkerList * msg )
{
    OUTPUT( "Process ( MsgRequestWorkerList )\n");

    MutexHolder mh( m_Mutex );
    
    MemoryStream ms;
    const size_t numWorkers( m_Workers.GetSize() );
    ms.Write( (uint32_t)numWorkers );
    if (msg->GetShouldGetAllInfo())
    {
        for ( size_t i = 0; i < numWorkers; ++i )
        {
            if ( m_Workers[ i ].m_ProtocolVersion == msg->GetProtocolVersion() && m_Workers[ i ].m_Platform == msg->GetPlatform() )
            {
                ms.Write( m_Workers[ i ].m_Version );
                ms.Write( m_Workers[ i ].m_User );
                ms.Write( m_Workers[ i ].m_HostName );
                ms.Write( m_Workers[ i ].m_DomainName );
                ms.Write( m_Workers[ i ].m_Mode );
                ms.Write( m_Workers[ i ].m_AvailableCPUs );
                ms.Write( m_Workers[ i ].m_TotalCPUs );
                ms.Write( m_Workers[ i ].m_Memory );
                ms.Write( m_Workers[ i ].m_Address );
            }
        }

        const Protocol::MsgWorkerList resultMsg;
        resultMsg.Send( connection, ms );
    }
    else
    {
        for ( size_t i = 0; i < numWorkers; ++i )
        {
            if ( m_Workers[ i ].m_ProtocolVersion == msg->GetProtocolVersion() && m_Workers[ i ].m_Platform == msg->GetPlatform() )
            {
                ms.Write( m_Workers[ i ].m_Address );
            }
        }

        const Protocol::MsgWorkerList resultMsg;
        resultMsg.Send( connection, ms );
    }
}

// Process ( MsgWorkerList )
//------------------------------------------------------------------------------
void WorkerConnectionPool::Process( const ConnectionInfo * connection, const Protocol::MsgWorkerList * /*msg*/, const void * payload, size_t payloadSize )
{
    OUTPUT( "Process ( MsgWorkerList )\n");

    ConstMemoryStream ms( payload, payloadSize );

    uint32_t numWorkers( 0 );
    ms.Read( numWorkers );

    OUTPUT( "%u workers in payload\n", numWorkers );

    Array< uint32_t  > workers;
    workers.SetCapacity( numWorkers );

    for ( size_t i=0; i<(size_t)numWorkers; ++i )
    {
        uint32_t workerAddress( 0 );
        ms.Read( workerAddress );
        workers.Append( workerAddress );
    }

    WorkerBrokerage * brokerage = ( WorkerBrokerage *)connection->GetUserData();
    ASSERT( brokerage );
    brokerage->UpdateWorkerList( workers );
}

// Process ( MsgSetWorkerStatus )
//------------------------------------------------------------------------------
void WorkerConnectionPool::Process( const ConnectionInfo * connection, const Protocol::MsgSetWorkerStatus * msg )
{
    MutexHolder mh( m_Mutex );

    const uint32_t workerAddress = connection->GetRemoteAddress();
    if ( msg->IsAvailable() )
    {
        if ( m_Workers.Find( workerAddress ) == nullptr )
        {
            AStackString<> remoteAddr;
            TCPConnectionPool::GetAddressAsString( workerAddress, remoteAddr );
            OUTPUT( "New worker available: %s\n", remoteAddr.Get() );
            m_Workers.Append( WorkerInfo( workerAddress, msg->GetProtocolVersion(), msg->GetPlatform() ) );
        }
    }
    else
    {
        m_Workers.FindAndErase( workerAddress );
    }
}

// Process ( MsgUpdateWorkerInfo )
//------------------------------------------------------------------------------
void WorkerConnectionPool::Process( const ConnectionInfo * connection, const Protocol::MsgUpdateWorkerInfo * /*msg*/, const void * payload, size_t payloadSize )
{
    MutexHolder mh( m_Mutex );

    const uint32_t workerAddress = connection->GetRemoteAddress();
    WorkerInfo* workerInfo = m_Workers.Find(workerAddress);
    if ( workerInfo != nullptr)
    {
        ConstMemoryStream ms( payload, payloadSize );
        ms.Read(workerInfo->m_Version);
        ms.Read(workerInfo->m_User);
        ms.Read(workerInfo->m_HostName);
        ms.Read(workerInfo->m_DomainName);
        ms.Read(workerInfo->m_Mode);
        ms.Read(workerInfo->m_AvailableCPUs);
        ms.Read(workerInfo->m_TotalCPUs);
        ms.Read(workerInfo->m_Memory);
    }
}

//------------------------------------------------------------------------------

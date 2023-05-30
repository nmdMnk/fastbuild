// WorkerConnectionPool
//------------------------------------------------------------------------------
#pragma once

// Includes
//------------------------------------------------------------------------------

// Core
#include "Core/Network/TCPConnectionPool.h"

// Forward Declarations
//------------------------------------------------------------------------------
namespace Protocol
{
    class IMessage;
    class MsgRequestWorkerList;
    class MsgWorkerList;
    class MsgSetWorkerStatus;
    class MsgUpdateWorkerInfo;
}

// WorkerInfo
//------------------------------------------------------------------------------
struct WorkerInfo
{
    WorkerInfo() {}
    WorkerInfo( uint32_t address, uint32_t protocolVersion, uint8_t platform )
        : m_Address( address )
        , m_ProtocolVersion( protocolVersion )
        , m_Platform( platform )
    {}

    bool operator == ( uint32_t address ) const { return address == m_Address; }
    
    AString     m_Version;
    AString     m_User;
    AString     m_HostName;
    AString     m_DomainName;
    AString     m_Mode;
    uint32_t    m_AvailableCPUs;
    uint32_t    m_TotalCPUs;
    uint32_t    m_Memory;
    
    uint32_t    m_Address;
    uint32_t    m_ProtocolVersion;
    uint8_t     m_Platform;
};

// WorkerConnectionPool
//------------------------------------------------------------------------------
class WorkerConnectionPool : public TCPConnectionPool
{
public:
    WorkerConnectionPool();
    virtual ~WorkerConnectionPool() override;

private:
    // network events - NOTE: these happen in another thread! (but never at the same time)
    virtual void OnReceive( const ConnectionInfo *, void * /*data*/, uint32_t /*size*/, bool & /*keepMemory*/ ) override;
    virtual void OnConnected( const ConnectionInfo * ) override;
    virtual void OnDisconnected( const ConnectionInfo * ) override;

    void Process( const ConnectionInfo * connection, const Protocol::MsgRequestWorkerList * msg );
    void Process( const ConnectionInfo * connection, const Protocol::MsgWorkerList * msg, const void * payload, size_t payloadSize );
    void Process( const ConnectionInfo * connection, const Protocol::MsgSetWorkerStatus * msg );
    void Process( const ConnectionInfo * connection, const Protocol::MsgUpdateWorkerInfo * msg, const void * payload, size_t payloadSize );

    Mutex                       m_Mutex;
    Array< WorkerInfo >         m_Workers;
    const Protocol::IMessage    * m_CurrentMessage;
};

//------------------------------------------------------------------------------

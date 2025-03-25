// WorkerBrokerage - Manage worker discovery
//------------------------------------------------------------------------------
#pragma once

// Includes
//------------------------------------------------------------------------------
#include "Core/Strings/AString.h"

// Forward Declarations
//------------------------------------------------------------------------------
class WorkerConnectionPool;
class ConnectionInfo;

// WorkerBrokerage
//------------------------------------------------------------------------------
class WorkerBrokerage
{
public:
    WorkerBrokerage();
    ~WorkerBrokerage();

    const AString& GetBrokerageRootPaths() const { return m_BrokerageRootPaths; }

    // client interface
    void FindWorkers( Array<AString>& workerList );
    void UpdateWorkerList( Array<uint32_t>& workerListUpdate );
protected:
    void InitBrokerage();
    void UpdateBrokerageFilePath();

    bool ConnectToCoordinator();
    void DisconnectFromCoordinator();

    Array<AString>      m_BrokerageRoots;
    AString             m_BrokerageRootPaths;
    bool                m_BrokerageInitialized;
    AString             m_HostName;
    AString             m_IPAddress;
    bool                m_PreferHostName = false;
    AString             m_BrokerageFilePath;
    AString             m_CoordinatorAddress;
    WorkerConnectionPool * m_ConnectionPool;
    const ConnectionInfo * m_Connection;
    Array<uint32_t>     m_WorkerListUpdate;
    bool                m_WorkerListUpdateReady;
};

//------------------------------------------------------------------------------

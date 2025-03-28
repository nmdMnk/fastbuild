// WorkerBrokerageServer - Server-side worker registration
//------------------------------------------------------------------------------
#pragma once

// Includes
//------------------------------------------------------------------------------
// FBuild
#include "Tools/FBuild/FBuildCore/WorkerPool/WorkerBrokerage.h"

// Core
#include "Core/Time/Timer.h"

// Forward Declarations
//------------------------------------------------------------------------------
class WorkerConnectionPool;
class ConnectionInfo;

// WorkerBrokerageServer
//------------------------------------------------------------------------------
class WorkerBrokerageServer : public WorkerBrokerage
{
public:
    WorkerBrokerageServer( bool preferHostName );
    ~WorkerBrokerageServer() override;

    void SetAvailability( bool available );

    const AString & GetHostName() const { return m_HostName; }

protected:
    void UpdateBrokerageFilePath();

    void SetAvailabilityToBrokerage( bool available );
    void SetAvailabilityToCoordinator( bool available );

    Timer               m_TimerLastUpdate;      // Throttle network access
    Timer               m_TimerLastIPUpdate;    // Throttle dns access
    Timer               m_TimerLastCleanBroker;
    uint64_t            m_SettingsWriteTime = 0; // FileTime of settings time when last changed
    bool                m_Available = false;
    bool                m_PreferHostName = false;
    AString             m_BrokerageFilePath;
    AString             m_IPAddress;
    AString             m_DomainName;
    AString             m_HostName;

};

//------------------------------------------------------------------------------

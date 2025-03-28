// Worker
//------------------------------------------------------------------------------
#pragma once

// Includes
//------------------------------------------------------------------------------
#include "IdleDetection.h"

// FBuild
#include "Tools/FBuild/FBuildCore/WorkerPool/WorkerBrokerageServer.h"

// Core
#include "Core/Containers/Singleton.h"
#include "Core/Env/MSVCStaticAnalysis.h"
#include "Core/FileIO/FileStream.h"

// Forward Declarations
//------------------------------------------------------------------------------
class Server;
class WorkerWindow;
class JobQueueRemote;
class NetworkStartupHelper;
class WorkerSettings;

// Worker
//------------------------------------------------------------------------------
class Worker : public Singleton<Worker>
{
public:
    explicit Worker( const AString & args, bool consoleMode, bool periodicRestart, bool preferHostName );
    ~Worker();

    int32_t Work();

    void SetWantToQuit() { m_WantToQuit = true; }

    void SetCoordinatorAddress(const AString & address) { m_WorkerBrokerage.SetCoordinatorAddress(address); }
    void SetBrokeragePath(const AString & path) { m_WorkerBrokerage.SetBrokeragePath(path); }
private:
    static uint32_t WorkThreadWrapper( void * userData );
    uint32_t WorkThread();

    void UpdateAvailability();
    void UpdateUI();
    void CheckIfRestartNeeded();
    bool HasEnoughDiskSpace();
    bool HasEnoughMemory();

    inline bool InConsoleMode() const { return m_ConsoleMode; }

    void StatusMessage( MSVC_SAL_PRINTF const char * fmtString, ... ) const FORMAT_STRING( 2, 3 );
    void ErrorMessage( MSVC_SAL_PRINTF const char * fmtString, ... ) const FORMAT_STRING( 2, 3 );

    bool                m_ConsoleMode;
    bool                m_PeriodicRestart;
    WorkerWindow        * m_MainWindow;
    Server              * m_ConnectionPool;
    NetworkStartupHelper * m_NetworkStartupHelper;
    WorkerSettings      * m_WorkerSettings;
    IdleDetection       m_IdleDetection;
    WorkerBrokerageServer m_WorkerBrokerage;
    AString             m_BaseExeName;
    AString             m_BaseArgs;
    uint64_t            m_LastWriteTime;
    bool                m_WantToQuit;
    bool                m_RestartNeeded;
    Timer               m_PeriodicRestartTimer;
    Timer               m_UIUpdateTimer;
    FileStream          m_TargetIncludeFolderLock;
    #if defined( __WINDOWS__ )
        Timer               m_TimerLastDiskSpaceCheck;
        int32_t             m_LastDiskSpaceResult;      // -1 : No check done yet. 0=Not enough space right now. 1=OK for now.

        Timer               m_TimerLastMemoryCheck;
        int32_t             m_LastMemoryCheckResult;    // -1 : No check done yet. 0=Not enough memory right now. 1=OK for now.
#endif
    mutable AString     m_LastStatusMessage;
};

//------------------------------------------------------------------------------

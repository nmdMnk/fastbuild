// Main
//------------------------------------------------------------------------------
#pragma once

// Includes
//------------------------------------------------------------------------------
// FBuild
#include "Tools/FBuild/FBuildWorker/Worker/WorkerSettings.h"

// Core
#include "Core/Env/Types.h"
#include "Core/Strings/AString.h"

// Forward Declaration
//------------------------------------------------------------------------------
class AString;

// FBuildWorkerOptions
//------------------------------------------------------------------------------
class FBuildWorkerOptions
{
public:
    FBuildWorkerOptions();

    bool ProcessCommandLine( const AString & commandLine );

    // sub-process control
    #if defined( __WINDOWS__ )
        bool m_IsSubprocess;    // Process is child? (Internal)
        bool m_UseSubprocess;   // Should we launch a sub-process?
    #endif

    // resource usage
    bool m_OverrideCPUAllocation;
    uint32_t m_CPUAllocation;
    bool m_OverrideWorkMode;
    WorkerSettings::Mode m_WorkMode;
    uint32_t m_MinimumFreeMemoryMiB; // Minimum OS free memory including virtual memory to let worker do its work

    // Console mode
    bool m_ConsoleMode;

    // Other
    bool m_PeriodicRestart;
    bool m_PreferHostName;

    // Coordinator ip
    AString m_CoordinatorAddress;

    // Brokerage path
    AString m_BrokeragePath;

private:
    void ShowUsageError();
};

//------------------------------------------------------------------------------

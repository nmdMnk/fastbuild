// Main
//------------------------------------------------------------------------------

// Includes
//------------------------------------------------------------------------------

// FBuildWorker
#include "Tools/FBuild/FBuildWorker/FBuildWorkerOptions.h"
#include "Tools/FBuild/FBuildWorker/Worker/Worker.h"

// Core
#include "Core/Env/Assert.h"
#include "Core/Env/Env.h"
#include "Core/Env/ErrorFormat.h"
#include "Core/FileIO/FileIO.h"
#include "Core/Mem/MemTracker.h"
#include "Core/Process/Process.h"
#include "Core/Process/SystemMutex.h"
#include "Core/Process/Thread.h"
#include "Core/Profile/Profile.h"
#include "Core/Strings/AStackString.h"

// system
#if defined( __WINDOWS__ )
    #include "Core/Env/WindowsHeader.h"
#endif

#if defined( __APPLE__ )
    #include <sys/resource.h>
    #include <limits.h>
#endif

// Global Data
//------------------------------------------------------------------------------
// only allow 1 worker per system
static SystemMutex g_OneProcessMutex( "Global\\FBuildWorker" );

// Functions
//------------------------------------------------------------------------------
int Main( const AString & args );
#if defined( __WINDOWS__ )
    int LaunchSubProcess( const AString & args );
#endif

//------------------------------------------------------------------------------
#if defined( __WINDOWS__ )
    PRAGMA_DISABLE_PUSH_MSVC( 28251 ) // don't complain about missing annotations on WinMain
    int WINAPI WinMain( HINSTANCE /*hInstance*/,
                        HINSTANCE /*hPrevInstance*/,
                        LPSTR lpCmdLine,
                        int /*nCmdShow*/ )
    {
        AStackString<> args( lpCmdLine );
        const int32_t result = Main( args );
        PROFILE_SYNCHRONIZE
        return result;
    }
    PRAGMA_DISABLE_POP_MSVC
#else
    int main( int argc, char ** argv )
    {
        AStackString<> args;
        for ( int i = 1; i < argc; ++i ) // NOTE: Skip argv[0] exe name
        {
            if ( i > 0 )
            {
                args += ' ';
            }
            args += argv[ i ];
        }
        const int32_t result = Main( args );
        PROFILE_SYNCHRONIZE
        return result;
    }
#endif

#include <stdio.h>

// Main
//------------------------------------------------------------------------------
int Main( const AString & args )
{
    PROFILE_FUNCTION;

    // don't buffer output
    VERIFY( setvbuf( stdout, nullptr, _IONBF, 0 ) == 0 );
    VERIFY( setvbuf( stderr, nullptr, _IONBF, 0 ) == 0 );

    // process cmd line args
    FBuildWorkerOptions options;
    if ( options.ProcessCommandLine( args ) == false )
    {
        return -3;
    }

    // only allow 1 worker per system
    const Timer t;
    while ( g_OneProcessMutex.TryLock() == false )
    {
        // retry for upto 2 seconds, to allow some time for old worker to close
        if ( t.GetElapsed() > 5.0f )
        {
            Env::ShowMsgBox( "FBuildWorker", "An FBuildWorker is already running!" );
            return -1;
        }
        Thread::Sleep( 100 );
    }

    #if defined( __WINDOWS__ )
        if ( options.m_UseSubprocess && !options.m_IsSubprocess )
        {
            return LaunchSubProcess( args );
        }
    #endif

    // prevent popups when launching tools with missing dlls
    #if defined( __WINDOWS__ )
        ::SetErrorMode( SEM_FAILCRITICALERRORS );
    #else
        // TODO:MAC SetErrorMode equivalent
        // TODO:LINUX SetErrorMode equivalent
    #endif

    #if defined( __WINDOWS__ )
        VERIFY( SetPriorityClass( GetCurrentProcess(), BELOW_NORMAL_PRIORITY_CLASS ) );
    #else
        // TODO:MAC SetPriorityClass equivalent
        // TODO:LINUX SetPriorityClass equivalent
    #endif

    // increase the limit of simultaneously open files on macOS to maximum allowed
    #if defined( __APPLE__)
        struct rlimit limit = { OPEN_MAX, RLIM_INFINITY };
        setrlimit( RLIMIT_NOFILE, &limit );
    #endif

    // start the worker and wait for it to be closed
    int ret;
    {
        Worker worker( args, options.m_ConsoleMode, options.m_PeriodicRestart, options.m_PreferHostName );
        if ( options.m_OverrideCPUAllocation )
        {
            WorkerSettings::Get().SetNumCPUsToUse( options.m_CPUAllocation );
        }
        if ( options.m_OverrideWorkMode )
        {
            WorkerSettings::Get().SetMode( options.m_WorkMode );
        }
        if ( options.m_MinimumFreeMemoryMiB )
        {
            WorkerSettings::Get().SetMinimumFreeMemoryMiB( options.m_MinimumFreeMemoryMiB );
        }
        if ( !options.m_CoordinatorAddress.IsEmpty() )
        {
            worker.SetCoordinatorAddress( options.m_CoordinatorAddress );
        }
        if ( !options.m_BrokeragePath.IsEmpty() )
        {
            worker.SetBrokeragePath( options.m_BrokeragePath );
        }
        ret = worker.Work();
    }

    return ret;
}

// LaunchSubProcess
//------------------------------------------------------------------------------
#if defined( __WINDOWS__ )
    int LaunchSubProcess( const AString & args )
    {
        // try to make a copy of our exe
        AStackString<> exeName;
        Env::GetExePath( exeName );
        AStackString<> exeNameCopy( exeName );
        exeNameCopy += ".copy";
        const Timer t;
        while ( FileIO::FileCopy( exeName.Get(), exeNameCopy.Get() ) == false )
        {
            if ( t.GetElapsed() > 5.0f )
            {
                AStackString<> msg;
                msg.Format( "Failed to make sub-process copy. Error: %s\n\nSrc: %s\nDst: %s\n", LAST_ERROR_STR, exeName.Get(), exeNameCopy.Get() );
                Env::ShowMsgBox( "FBuildWorker", msg.Get() );
                return -2;
            }
            Thread::Sleep( 100 );
        }

        AStackString<> argsCopy( args );
        argsCopy += " -subprocess";

        // allow subprocess to access the mutex
        g_OneProcessMutex.Unlock();

        Process p;
        #if defined( __WINDOWS__ )
            p.DisableHandleRedirection(); // TODO:MAC TODO:LINUX is this needed?
        #endif
        (void)p.Spawn( exeNameCopy.Get(), argsCopy.Get(), nullptr, nullptr );
        p.Detach();

        return 0;
    }
#endif

//------------------------------------------------------------------------------

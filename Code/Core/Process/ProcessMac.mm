// ProcessMac.mm

// merge from  https://github.com/Incanta/fastbuild-unreal/blob/unreal/Code/Core/Process/ProcessMac.mm 
// fix some compile error

//------------------------------------------------------------------------------

// Includes
//------------------------------------------------------------------------------
#include "Process.h"

#if defined ( APPLE_PROCESS_USE_NSTASK )

#include "Core/Env/Assert.h"
#include "Core/Math/Conversions.h"
#include "Core/Process/Atomic.h"
#include "Core/Process/Thread.h"
#include "Core/Profile/Profile.h"
#include "Core/Strings/AString.h"
#include "Core/Time/Timer.h"

// Static Data
//------------------------------------------------------------------------------

// CONSTRUCTOR
//------------------------------------------------------------------------------
Process::Process( const volatile bool * masterAbortFlag,
                  const volatile bool * abortFlag )
: m_Started( false )
    , m_Task( nil )
    , m_StdOutRead( nil )
    , m_StdErrRead( nil )
    , m_HasAborted( false )
    , m_MainAbortFlag( masterAbortFlag )
    , m_AbortFlag( abortFlag )
{
}

// DESTRUCTOR
//------------------------------------------------------------------------------
Process::~Process()
{
    @autoreleasepool
    {
        if ( m_Started )
        {
            WaitForExit();
        }

        if ( m_Task )
        {
            [m_Task release];
            m_Task = nil;
        }
    }
}

// KillProcessTree
//------------------------------------------------------------------------------
void Process::KillProcessTree()
{
    // Kill all processes in the process group of the child process.
    kill( -m_Task.processIdentifier, SIGKILL );
}

// Spawn
//------------------------------------------------------------------------------
bool Process::Spawn( const char * executable,
                     const char * args,
                     __attribute__((unused)) const char * workingDir,
                     __attribute__((unused)) const char * environment,
                     __attribute__((unused)) bool shareHandles )
{
    PROFILE_FUNCTION;

    ASSERT( !m_Started );
    ASSERT( executable );

    if ( m_MainAbortFlag && AtomicLoadRelaxed( m_MainAbortFlag ) )
    {
        // Once master process has aborted, we no longer permit spawning sub-processes.
        return false;
    }

    @autoreleasepool {

    m_Task = [NSTask new];

    [m_Task setLaunchPath:[NSString stringWithUTF8String:executable]];

    NSArray<NSString *> * temp = [[NSString stringWithUTF8String:args] componentsSeparatedByString:@" "];
    NSMutableArray * arguments = [[NSMutableArray alloc] init];

    for (NSUInteger i = 0; i < [temp count]; i++)
    {
        NSString * arg = ( NSString * )[temp objectAtIndex:i];

        if (/*[arg hasPrefix:@"\""] && */[arg hasSuffix:@"\""])
        {
            arg = [arg stringByReplacingOccurrencesOfString:@"\"" withString:@""];
        }
        [arguments addObject:arg];
    }

    m_StdOutRead = [NSPipe pipe];
    m_StdErrRead = [NSPipe pipe];
    [m_Task setStandardOutput:m_StdOutRead];
    [m_Task setStandardError:m_StdErrRead];

    [m_Task setArguments:arguments];

    [m_Task launch];

    [arguments release];

    m_Started = true;

    } // @autoreleasepool

    return true;
}

// IsRunning
//----------------------------------------------------------
bool Process::IsRunning() const
{
    ASSERT( m_Started );

    @autoreleasepool
    {
        return [m_Task isRunning];
    }
}

// WaitForExit
//------------------------------------------------------------------------------
int32_t Process::WaitForExit()
{
    ASSERT( m_Started );
    m_Started = false;

    @autoreleasepool
    {
        [m_Task waitUntilExit];
 
        return [m_Task terminationStatus];
    }
}

// Detach
//------------------------------------------------------------------------------
void Process::Detach()
{
    ASSERT( m_Started );
    m_Started = false;

    ASSERT( false ); // TODO:Mac Implement Process
}

// ReadAllData
//------------------------------------------------------------------------------
bool Process::ReadAllData( AString & outMem,
                           AString & errMem,
                           __attribute__((unused)) uint32_t timeOutMS )
{
    @autoreleasepool {

    __block bool stdOutIsDone = false;
    __block bool stdErrIsDone = false;

    NSFileHandle * stdOutHandle = [m_StdOutRead fileHandleForReading];

    dispatch_async( dispatch_get_global_queue( DISPATCH_QUEUE_PRIORITY_LOW, 0 ), ^{
    @autoreleasepool {
        NSData * stdOutData = [stdOutHandle availableData];
        while ( [stdOutData length] > 0 )
        {
            const bool masterAbort = ( m_MainAbortFlag && AtomicLoadRelaxed( m_MainAbortFlag ) );
            const bool abort = ( m_AbortFlag && AtomicLoadRelaxed( m_AbortFlag ) );
            if ( abort || masterAbort )
            {
                PROFILE_SECTION( "Abort" );
                KillProcessTree();
                m_HasAborted = true;
                break;
            }

            Read( stdOutData, outMem );

            stdOutData = [stdOutHandle availableData];
        }

        stdOutIsDone = true;
    }});

    NSFileHandle * stdErrHandle = [m_StdErrRead fileHandleForReading];

    dispatch_async( dispatch_get_global_queue( DISPATCH_QUEUE_PRIORITY_LOW, 0 ), ^{
    @autoreleasepool {
        NSData * stdErrData = [stdErrHandle availableData];
        while ( [stdErrData length] > 0 )
        {
            const bool masterAbort = ( m_MainAbortFlag && AtomicLoadRelaxed( m_MainAbortFlag ) );
            const bool abort = ( m_AbortFlag && AtomicLoadRelaxed( m_AbortFlag ) );
            if ( abort || masterAbort )
            {
                PROFILE_SECTION( "Abort" );
                KillProcessTree();
                m_HasAborted = true;
                break;
            }

            Read( stdErrData, errMem );

            stdErrData = [stdErrHandle availableData];
        }

        stdErrIsDone = true;
    }});

    [m_Task waitUntilExit];

    // async tasks may still be processing the child process output, so wait for them if needed
    while ( !stdOutIsDone || !stdErrIsDone )
    {
        sched_yield();
    }

    } // @autoreleasepool

    return true;
}

// Read
//------------------------------------------------------------------------------
void Process::Read( NSData * availableData, AString & buffer )
{
    @autoreleasepool {

    uint32_t bytesAvail = [availableData length];

    if ( bytesAvail == 0 )
    {
        return;
    }

    // will it fit in the buffer we have?
    const uint32_t sizeSoFar = buffer.GetLength();
    const uint32_t newSize = ( sizeSoFar + bytesAvail );
    if ( newSize > buffer.GetReserved() )
    {
        // Expand buffer for new data in large chunks
        const uint32_t newBufferSize = Math::Max< uint32_t >( newSize, buffer.GetReserved() + ( 16 * MEGABYTE ) );
        buffer.SetReserved( newBufferSize );
    }

    ASSERT( buffer.Get() );

    [availableData getBytes:(buffer.Get() + sizeSoFar) length:bytesAvail];

    // Update length
    buffer.SetLength( sizeSoFar + bytesAvail );

    } // @autoreleasepool
}

// GetCurrentId
//------------------------------------------------------------------------------
/*static*/ uint32_t Process::GetCurrentId()
{
    return ::getpid();
}

// Terminate
//------------------------------------------------------------------------------
void Process::Terminate()
{
    kill( m_Task.processIdentifier, SIGKILL );
}

bool Process::HasAborted() const
{
    return ( m_MainAbortFlag && AtomicLoadRelaxed( m_MainAbortFlag ) ) ||
           ( m_AbortFlag && AtomicLoadRelaxed( m_AbortFlag ) );
}


//------------------------------------------------------------------------------

#endif // APPLE_PROCESS_USE_NSTASK

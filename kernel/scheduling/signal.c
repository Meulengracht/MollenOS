/* MollenOS
 *
 * Copyright 2017, Philip Meulengracht
 *
 * This program is free software : you can redistribute it and / or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation ? , either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * Threading Signal Implementation
 */
#define __MODULE "SIG0"
//#define __TRACE

#include <arch/thread.h>
#include <arch/utils.h>
#include <memoryspace.h>
#include <threading.h>
#include <scheduler.h>
#include <string.h>
#include <handle.h>
#include <debug.h>
#include <heap.h>

#if 0
static char SignalFatalityTable[] = {
    0, /* 0? */
    1, /* SIGHUP     */
    1, /* SIGINT     */
    2, /* SIGQUIT    */
    2, /* SIGILL     */
    2, /* SIGTRAP    */
    2, /* SIGABRT    */
    2, /* SIGEMT     */
    2, /* SIGFPE     */
    1, /* SIGKILL    */
    2, /* SIGBUS     */
    2, /* SIGSEGV    */
    2, /* SIGSYS     */
    1, /* SIGPIPE    */
    1, /* SIGALRM    */
    1, /* SIGTERM    */
    0, /* SIGUSR1    */
    0, /* SIGUSR2    */
    0, /* SIGCHLD    */
    0, /* SIGPWR     */
    0, /* SIGWINCH   */
    0, /* SIGURG     */
    0, /* SIGPOLL    */
    3, /* SIGSTOP    */
    3, /* SIGTSTP    */
    0, /* SIGCONT    */
    3, /* SIGTTIN    */
    3, /* SIGTTOUT   */
    1, /* SIGVTALRM  */
    1, /* SIGPROF    */
    2, /* SIGXCPU    */
    2, /* SIGXFSZ    */
    0, /* SIGWAITING */
    1, /* SIGDIAF    */
    0, /* SIGHATE    */
    0, /* SIGWINEVENT*/
    0, /* SIGCAT     */
    0  /* SIGEND     */
};
#endif

OsStatus_t
SignalCreateExternal(
    _In_ UUId_t ThreadId,
    _In_ int    Signal,
    _In_ void*  Argument)
{
    MCoreThread_t* Target   = GetThread(ThreadId);
    int            Expected = SIGNAL_FREE;
    TRACE("SignalCreateExternal(Thread %" PRIuIN ", Signal %i)", ThreadId, Signal);

    // Sanitize input, and then sanitize if we have a handler
    if (Target == NULL || Signal >= NUMSIGNALS) {
        ERROR("Signal %i was not in range");
        return OsInvalidParameters; // Invalid
    }
    
    if (!atomic_compare_exchange_strong(&Target->Signals[Signal].Status, 
            &Expected, SIGNAL_ALLOCATED)) {
        return OsExists; // Ignored, already pending
    }
    
    // Store information and mark ready
    atomic_store(&Target->Signals[Signal].Information, Argument);
    atomic_store(&Target->Signals[Signal].Status, SIGNAL_PENDING);
    
    // Wake up thread if neccessary
    SchedulerExpediteObject(Target->SchedulerObject);
    return OsSuccess;
}

OsStatus_t
SignalCreateInternal(
    _In_ Context_t* Registers,
    _In_ int        Signal,
    _In_ void*      Argument)
{
    UUId_t         CoreId = ArchGetProcessorCoreId();
    MCoreThread_t* Thread = GetCurrentThreadForCore(CoreId);

    TRACE("ExceptionSignal(Signal %i)", Signal);
    __asm { xchg bx, bx };

    // Sanitize if user-process
#ifdef __OSCONFIG_DISABLE_SIGNALLING
    if (Signal >= 0) {
#else
    if (Thread == NULL || Thread->MemorySpace == NULL ||
        Thread->MemorySpace->Context == NULL ||
        Thread->MemorySpace->Context->SignalHandler == 0) {
#endif
        return OsError;
    }

    // We do absolutely not care about the existing signal stack
    // in case of internal signals
    if (!Thread->HandlingSignals) {
        Thread->HandlingSignals = 1;
        Thread->OriginalContext = Registers;
    }
    ContextReset(Thread->Contexts[THREADING_CONTEXT_SIGNAL],
        THREADING_CONTEXT_SIGNAL, Thread->MemorySpace->Context->SignalHandler,
        (uintptr_t)Signal, (uintptr_t)Argument, 0);
    Thread->ContextActive = Thread->Contexts[THREADING_CONTEXT_SIGNAL];
    return OsSuccess;
}

void
SignalProcess(
    _In_ UUId_t ThreadId)
{
    MCoreThread_t* Thread = GetThread(ThreadId);
    int            i;

    // Protect against signals received
    if (Thread == NULL || 
        Thread->MemorySpace == NULL ||
        Thread->MemorySpace->Context == NULL ||
        Thread->MemorySpace->Context->SignalHandler == 0 ||
        Thread->Contexts[THREADING_CONTEXT_SIGNAL] == NULL) {
        return;
    }
    
    // Are we already handling signals?
    if (Thread->HandlingSignals) {
        return;
    }
    
    // Process all the signals
    for (i = 0; i < NUMSIGNALS; i++) {
        int Status = atomic_load(&Thread->Signals[i].Status);
        if (Status == SIGNAL_PENDING) {
            void* Argument = atomic_load(&Thread->Signals[i].Information);
            // Prepare initial stack state
            if (!Thread->HandlingSignals) {
                Thread->HandlingSignals = 1;
                Thread->OriginalContext = Thread->ContextActive;
                Thread->ContextActive   = Thread->Contexts[THREADING_CONTEXT_SIGNAL];
                ContextReset(Thread->Contexts[THREADING_CONTEXT_SIGNAL],
                    THREADING_CONTEXT_SIGNAL, Thread->MemorySpace->Context->SignalHandler,
                    i, (uintptr_t)Argument, 0);
            }
            else {
                // Just push the interceptor
                ContextPushInterceptor(Thread->ContextActive,
                    Thread->MemorySpace->Context->SignalHandler,
                    i, (uintptr_t)Argument, 0);
            }
            atomic_store(&Thread->Signals[i].Status, SIGNAL_FREE);
        }
    }
}

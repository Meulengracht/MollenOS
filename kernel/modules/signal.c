/* MollenOS
 *
 * Copyright 2011 - 2017, Philip Meulengracht
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
 * MollenOS MCore - Signal Implementation
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

static char GlbSignalIsDeadly[] = {
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

static Context_t*
EnsureSignalStack(
    _In_ MCoreThread_t* Thread)
{
    if (Thread->Contexts[THREADING_CONTEXT_SIGNAL] == NULL) {
        Thread->Contexts[THREADING_CONTEXT_SIGNAL] = ContextCreate(THREADING_CONTEXT_SIGNAL);
    }
    ContextReset(Thread->Contexts[THREADING_CONTEXT_SIGNAL], THREADING_CONTEXT_SIGNAL, 
        0, 0, 0, 0);
    return Thread->Contexts[THREADING_CONTEXT_SIGNAL];
}

OsStatus_t
SignalCreateExternal(
    _In_ UUId_t ThreadId,
    _In_ int    Signal)
{
    MCoreThread_t* Target = GetThread(ThreadId);
    TRACE("SignalCreateExternal(Thread %" PRIuIN ", Signal %i)", ThreadId, Signal);

    // Sanitize input, and then sanitize if we have a handler
    if (Target == NULL || Signal >= NUMSIGNALS) {
        ERROR("Signal %i was not in range");
        return OsInvalidParameters; // Invalid
    }
    if (atomic_exchange(&Target->Signals[Signal].Pending, 1) == 1) {
        return OsExists; // Ignored
    }
    
    Target->Signals[Signal].Deadly = GlbSignalIsDeadly[Signal];
    
    // Wake up thread if neccessary
    SchedulerExpediteObject(Target->SchedulerObject);
    return OsSuccess;
}

OsStatus_t
SignalCreateInternal(
    _In_ Context_t* Registers,
    _In_ int        Signal)
{
    UUId_t         CoreId    = ArchGetProcessorCoreId();
    MCoreThread_t* Thread    = GetCurrentThreadForCore(CoreId);
    Context_t*     SafeStack = NULL;

    TRACE("ExceptionSignal(Signal %i)", Signal);

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

    // Ensure safe stack for deadly interrupts
    // 0 => not deadly
    // 1 => soft terminate
    // 2 => safe terminate
    // 3 => safe terminate (should quit immediately)
    if (GlbSignalIsDeadly[Signal] > 1) {
        // @todo check if works
        //SafeStack = EnsureSignalStacks(Thread);
    }

    // Push the intercept
    ContextPushInterceptor(Registers, 
        Thread->MemorySpace->Context->SignalHandler, 
        (uintptr_t*)SafeStack,
        Signal,
        0);
    return OsSuccess;
}

void
SignalProcess(
    _In_ UUId_t ThreadId)
{
    MCoreThread_t* Thread = GetThread(ThreadId);
    int            i;

    if (Thread == NULL || 
        Thread->MemorySpace == NULL ||
        Thread->MemorySpace->Context == NULL ||
        Thread->MemorySpace->Context->SignalHandler == 0) {
        return;
    }
    
    // Process all the signals
    for (i = 0; i < NUMSIGNALS; i++) {
        if (atomic_load(&Thread->Signals[i].Pending) == 1) {
            ContextPushInterceptor(Thread->ContextActive,
                Thread->MemorySpace->Context->SignalHandler,
                NULL,
                i,
                0);
            atomic_store(&Thread->Signals[i].Pending, 0);
        }
    }
}

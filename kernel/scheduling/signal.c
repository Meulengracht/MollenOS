/**
 * MollenOS
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
#include <assert.h>
#include <component/cpu.h>
#include <debug.h>
#include <handle.h>
#include <heap.h>
#include <machine.h>
#include <memoryspace.h>
#include <scheduler.h>
#include <string.h>
#include <threading.h>

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
SignalQueue(
    _In_ UUId_t ThreadId,
    _In_ int    Signal,
    _In_ void*  Argument)
{
    MCoreThread_t* Target   = (MCoreThread_t*)LookupHandle(ThreadId);
    int            Expected = SIGNAL_FREE;
    TRACE("SignalQueue(Thread %" PRIuIN ", Signal %i)", ThreadId, Signal);

    // Sanitize input, and then sanitize if we have a handler
    if (Target == NULL || Signal >= NUMSIGNALS) {
        ERROR("Signal %i was not in range");
        return OsInvalidParameters; // Invalid
    }
    
    if (!atomic_compare_exchange_strong(&Target->Signals[Signal].Status, 
            &Expected, SIGNAL_ALLOCATED)) {
        TRACE("Signal was already pending");
        return OsExists; // Ignored, already pending
    }
    
    // Store information and mark ready
    atomic_store(&Target->Signals[Signal].Information, Argument);
    atomic_store(&Target->Signals[Signal].Status, SIGNAL_PENDING);
    atomic_fetch_add(&Target->PendingSignals, 1);
    
    // Wake up thread if neccessary
    if (!Target->HandlingSignals) {
        TRACE("Waking up object");
        SchedulerExpediteObject(Target->SchedulerObject);
    }
    return OsSuccess;
}

void
SignalExecute(
    _In_ Context_t* Context,
    _In_ int        Signal,
    _In_ void*      Argument)
{
    SystemCpuCore_t* Core   = GetCurrentProcessorCore();
    MCoreThread_t*   Thread = Core->CurrentThread;
    
    TRACE("SignalExecute(Signal %i)", Signal);

    // Do not support signals that occur in kernel code, those should __NOT__ occur
    // but rather we should protect against or fix why it fails.
    if (IS_KERNEL_CODE(&GetMachine()->MemoryMap, CONTEXT_IP(Context))) {
        DebugPanic(FATAL_SCOPE_KERNEL, Context, "FAIL", 
            "Crash at address 0x%" PRIxIN, CONTEXT_IP(Context));
    }

#ifndef __OSCONFIG_DISABLE_SIGNALLING    
    // Must be a user process
    assert(Thread != NULL);
    assert(Thread->MemorySpace->Context != NULL);
    assert(Thread->MemorySpace->Context->SignalHandler != 0);

    // We do absolutely not care about the existing signal stack
    // in case of internal signals
    if (!Thread->HandlingSignals) {
        Thread->HandlingSignals = 1;
        Thread->OriginalContext = Context;
    }
    ContextReset(Thread->Contexts[THREADING_CONTEXT_SIGNAL],
        THREADING_CONTEXT_SIGNAL, Thread->MemorySpace->Context->SignalHandler,
        (uintptr_t)Signal, (uintptr_t)Argument, 0);
    Thread->ContextActive = Thread->Contexts[THREADING_CONTEXT_SIGNAL];
    
    // Switch to the signal context
    UpdateThreadContext(Thread, THREADING_CONTEXT_SIGNAL, 1);
#endif
}

void
SignalReturn(
    _In_ Context_t* Context)
{
    SystemCpuCore_t* Core   = GetCurrentProcessorCore();
    MCoreThread_t*   Thread = Core->CurrentThread;
    
    TRACE("SignalReturn()");
    assert(Thread != NULL);
    assert(Thread->OriginalContext != NULL);
    
    Thread->HandlingSignals = 0;
    Thread->ContextActive   = Thread->OriginalContext;
    Thread->OriginalContext = NULL;
    UpdateThreadContext(Thread, THREADING_CONTEXT_LEVEL0, 1);
}

void
SignalProcess(
    _In_ MCoreThread_t* Thread)
{
    int i;

    assert(Thread != NULL);
    
    // Protect against signals received
    if (Thread->MemorySpace->Context == NULL ||
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
            UpdateThreadContext(Thread, THREADING_CONTEXT_SIGNAL, 0);
            atomic_store(&Thread->Signals[i].Status, SIGNAL_FREE);
            atomic_fetch_sub(&Thread->PendingSignals, 1);
        }
    }
}

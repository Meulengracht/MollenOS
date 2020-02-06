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

#define __MODULE "signal"
#define __TRACE

#include <arch/interrupts.h>
#include <arch/thread.h>
#include <arch/utils.h>
#include <assert.h>
#include <component/cpu.h>
#include <debug.h>
#include <handle.h>
#include <heap.h>
#include <internal/_signal.h>
#include <machine.h>
#include <memoryspace.h>
#include <scheduler.h>
#include <string.h>
#include <threading.h>

static void
ExecuteSignalOnCoreFunction(
    _In_ void* Context)
{
    MCoreThread_t* Thread = Context;
    
    // This function has determine the course of action for the given thread.
    // CASE 1: Thread is currently running, Modify the current context and push
    //         the new signal handlers. The current context can be retrieved from
    //         the current core structure
    if (GetCurrentThreadForCore(ArchGetProcessorCoreId()) == Thread) {
        SystemCpuCore_t* Core = GetCurrentProcessorCore();
        
        // CASE 1.1: The thread is currently executing kernel code (syscall).
        //           In this case we must leave the system call be in queue,
        //           and handle this in the scheduler. Before scheduling we must
        //           check if any signals are queued, and if they are, interrupt
        //           the thread and requeue it.
        // CASE 1.2: The thread is currently executing user code. In this case
        //           we can simply process all the queued signals onto the current
        //           context.
        if (!IS_KERNEL_CODE(&GetMachine()->MemoryMap, CONTEXT_IP(Core->InterruptRegisters))) {
            SignalProcessQueued(Thread, Core->InterruptRegisters);
        }
    }
    
    // CASE 2: The thread is queued to run or currently blocked. So we need to
    //         expedite the thread in case of a block.
    else {
        
        // CASE 1.1: The thread is currently executing kernel code (syscall).
        //           In this case the signal must stay queued and be handled
        //           on exit of system call
        if (IS_KERNEL_CODE(&GetMachine()->MemoryMap, CONTEXT_IP(Thread->ContextActive))) {
            SchedulerExpediteObject(Thread->SchedulerObject);
        }
        
        // CASE 1.2: The thread is currently executing user code. In this case
        //           we can simply process all the queued signals onto the current
        //           context. In this case we can also safely assume the thread is 
        //           not currently blocked, as it would require a system call.
        else {
            SignalProcessQueued(Thread, Thread->ContextActive);
        }
    }
}

OsStatus_t
SignalSend(
    _In_ UUId_t ThreadId,
    _In_ int    Signal,
    _In_ void*  Argument)
{
    MCoreThread_t*  Target   = (MCoreThread_t*)LookupHandleOfType(ThreadId, HandleTypeThread);
    int             Expected = SIGNAL_FREE;
    SystemSignal_t* SignalInfo;
    int             Result;
    UUId_t          TargetCore;
    
    if (Target == NULL) {
        ERROR("[signal] [send] thread %" PRIuIN " did not exist", ThreadId, Signal);
        return OsDoesNotExist;
    }

    if (Signal < 0 || Signal >= NUMSIGNALS) {
        ERROR("[signal] [send] signal %i was not in range");
        return OsInvalidParameters; // Invalid
    }
    
    TRACE("[signal] [send] thread %s, signal %i", Target->Name, Signal);
    
    SignalInfo = &Target->Signaling.Signals[Signal];
    Result     = atomic_compare_exchange_strong(&SignalInfo->Status, 
        &Expected, SIGNAL_ALLOCATED);
    if (!Result) {
        TRACE("Signal was already pending");
        return OsExists; // Ignored, already pending
    }
    
    // Store information and mark ready
    SignalInfo->Argument = Argument;
    atomic_store(&SignalInfo->Status, SIGNAL_PENDING);
    atomic_fetch_add(&Target->Signaling.SignalsPending, 1);
    
    // Is the thread local or foreign? We only handle signals locally on core,
    // so if it is running on a different core, we want to send an IPI and let
    // the local core handle this.
    TargetCore = SchedulerObjectGetAffinity(Target->SchedulerObject);
    if (TargetCore == ArchGetProcessorCoreId()) {
        ExecuteSignalOnCoreFunction(Target);
        return OsSuccess;
    }
    else {
        return TxuMessageSend(TargetCore, CpuFunctionCustom, ExecuteSignalOnCoreFunction, Target, 1);
    }
}

void
SignalExecuteLocalThreadTrap(
    _In_ Context_t* Context,
    _In_ int        Signal,
    _In_ void*      Argument)
{
    MCoreThread_t* Thread = GetCurrentThreadForCore(ArchGetProcessorCoreId());
    
    assert(Thread != NULL);
    
    TRACE("[signal] [execute_trap] signal %i", Signal);

    // Do not support signals that occur in kernel code, those should __NOT__ occur
    // but rather we should protect against or fix why it fails.
    // However if we wanted to support this, we could 
    if (IS_KERNEL_CODE(&GetMachine()->MemoryMap, CONTEXT_IP(Context))) {
        DebugPanic(FATAL_SCOPE_KERNEL, Context, "FAIL", 
            "Crash at address 0x%" PRIxIN, CONTEXT_IP(Context));
    }

#ifdef __OSCONFIG_DISABLE_SIGNALLING
    WARNING("[signal] [execute_trap] signals are DISABLED");
#else
    assert(Thread->MemorySpace->Context != NULL);
    assert(Thread->MemorySpace->Context->SignalHandler != 0);

    // We do absolutely not care about the existing signal stack
    // in case of local trap signals
    ContextPushInterceptor(Context, 
        (uintptr_t)Thread->Contexts[THREADING_CONTEXT_SIGNAL], 
        Thread->MemorySpace->Context->SignalHandler, Signal, 
        (uintptr_t)Argument, SIGNAL_SEPERATE_STACK | SIGNAL_HARDWARE_TRAP);
#endif
}

void
SignalReturnFromLocalTrap(
    _In_ Context_t* Context)
{
    MCoreThread_t* Thread = GetCurrentThreadForCore(ArchGetProcessorCoreId());
    
    TRACE("[signal] [return]");
    assert(Thread != NULL);
    assert(Thread->Signaling.OriginalContext != NULL);
    
    Thread->ContextActive                = Thread->Signaling.OriginalContext;
    Thread->Signaling.OriginalContext    = NULL;
    Thread->Signaling.HandlingTrapSignal = 0;
    UpdateThreadContext(Thread, THREADING_CONTEXT_LEVEL0, /* EnterContext: */ 1);
}

void
SignalProcessQueued(
    _In_ MCoreThread_t* Thread,
    _In_ Context_t*     Context)
{
    uintptr_t SignalStack;
    uintptr_t Handler;
    int       i;
    
    assert(Thread != NULL);
    assert(Context != NULL);
    
    // Protect against signals received before the signal handler
    // has been installed
    if (!Thread->MemorySpace->Context ||
        !Thread->MemorySpace->Context->SignalHandler) {
        return;
    }
    
    Handler     = Thread->MemorySpace->Context->SignalHandler;
    SignalStack = CONTEXT_SP(Context);
    for (i = 0; i < NUMSIGNALS; i++) {
        SystemSignal_t* SignalInfo = &Thread->Signaling.Signals[i];
        int             Status;
        
        Status = atomic_load(&SignalInfo->Status);
        if (Status == SIGNAL_PENDING) {
            if (SignalInfo->Flags & SIGNAL_SEPERATE_STACK) {
                // Missing implementation
                // SignalStack = SignalInfo->Stack;
            }
            
            ContextPushInterceptor(Context, SignalStack, Handler, i, 
                (uintptr_t)SignalInfo->Argument, SignalInfo->Flags);
            SignalStack = CONTEXT_SP(Context);
            
            atomic_store(&SignalInfo->Status, SIGNAL_FREE);
            atomic_fetch_sub(&Thread->Signaling.SignalsPending, 1);
        }
    }
}

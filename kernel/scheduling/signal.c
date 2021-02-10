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
//#define __TRACE

#include <arch/thread.h>
#include <arch/utils.h>
#include <assert.h>
#include <component/cpu.h>
#include <debug.h>
#include <ds/streambuffer.h>
#include <handle.h>
#include <internal/_signal.h>
#include <machine.h>
#include <scheduler.h>
#include <threading.h>
#include "threading_private.h"

static void
ExecuteSignalOnCoreFunction(
    _In_ void* Context)
{
    Thread_t* thread = Context;
    TRACE("[signal] [execute]");
    
    // This function has determine the course of action for the given thread.
    // CASE 1: thread is currently running, Modify the current context and push
    //         the new signal handlers. The current context can be retrieved from
    //         the current core structure
    if (ThreadCurrentForCore(ArchGetProcessorCoreId()) == thread) {
        SystemCpuCore_t* core           = CpuCoreCurrent();
        Context_t*       currentContext = CpuCoreInterruptContext(core);
        
        // CASE 1.1: The thread is currently executing kernel code (syscall).
        //           In this case we must leave the system call be in queue,
        //           and handle this in the scheduler. Before scheduling we must
        //           check if any signals are queued, and if they are, interrupt
        //           the thread and requeue it. <Do Nothing>
        
        // CASE 1.2: The thread is currently executing user code. In this case
        //           we can simply process all the queued signals onto the current
        //           context.
        if (!IS_KERNEL_CODE(&GetMachine()->MemoryMap, CONTEXT_IP(currentContext))) {
            TRACE("[signal] [execute] case 1.2");
            SignalProcessQueued(thread, currentContext);
        }
    }
    
    // CASE 2: The thread is queued to run or currently blocked. So we need to
    //         expedite the thread in case of a block.
    else {
        
        // CASE 2.1: The thread is currently executing kernel code (syscall).
        //           In this case the signal must stay queued and be handled
        //           on exit of system call
        if (IS_KERNEL_CODE(&GetMachine()->MemoryMap, CONTEXT_IP(thread->ContextActive))) {
            TRACE("[signal] [execute] case 2.1");
            SchedulerExpediteObject(thread->SchedulerObject);
        }
        
        // CASE 2.2: The thread is currently executing user code. In this case
        //           we can simply process all the queued signals onto the current
        //           context. In this case we can also safely assume the thread is 
        //           not currently blocked, as it would require a system call.
        else {
            TRACE("[signal] [execute] case 2.2");
            SignalProcessQueued(thread, thread->ContextActive);
        }
    }
}

OsStatus_t
SignalSend(
    _In_ UUId_t ThreadId,
    _In_ int    Signal,
    _In_ void*  Argument)
{
    Thread_t*      target = THREAD_GET(ThreadId);
    UUId_t         targetCore;
    ThreadSignal_t signalInfo = {
        .Signal   = Signal,
        .Argument = Argument,
        .Flags    = 0
    };
    
    if (!target) {
        ERROR("[signal] [send] thread %" PRIuIN " did not exist", ThreadId, Signal);
        return OsDoesNotExist;
    }

    if (Signal < 0 || Signal >= NUMSIGNALS) {
        ERROR("[signal] [send] signal %i was not in range");
        return OsInvalidParameters; // Invalid
    }
    
    if (target->Signaling.Mask & (1 << Signal)) {
        return OsBlocked;
    }
    
    TRACE("[signal] [send] thread %s, signal %i", target->Name, Signal);
    streambuffer_stream_out(target->Signaling.Signals, &signalInfo, sizeof(ThreadSignal_t), 0);
    atomic_fetch_add(&target->Signaling.Pending, 1);
    
    // Is the thread local or foreign? We only handle signals locally on core,
    // so if it is running on a different core, we want to send an IPI and let
    // the local core handle this.
    targetCore = SchedulerObjectGetAffinity(target->SchedulerObject);
    if (targetCore == ArchGetProcessorCoreId()) {
        ExecuteSignalOnCoreFunction(target);
        return OsSuccess;
    }
    else {
        return TxuMessageSend(targetCore, CpuFunctionCustom, ExecuteSignalOnCoreFunction, target, 1);
    }
}

void
SignalExecuteLocalThreadTrap(
    _In_ Context_t* Context,
    _In_ int        Signal,
    _In_ void*      Argument)
{
    Thread_t* Thread = ThreadCurrentForCore(ArchGetProcessorCoreId());
    
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
    assert(MemorySpaceSignalHandler(Thread->MemorySpace) != 0);

    // We do absolutely not care about the existing signal stack
    // in case of local trap signals
    ContextPushInterceptor(Context, 
        (uintptr_t)Thread->Contexts[THREADING_CONTEXT_SIGNAL],
        MemorySpaceSignalHandler(Thread->MemorySpace), Signal,
        (uintptr_t)Argument, SIGNAL_SEPERATE_STACK | SIGNAL_HARDWARE_TRAP);
#endif
}

void
SignalProcessQueued(
    _In_ Thread_t*  Thread,
    _In_ Context_t* Context)
{
    ThreadSignal_t threadSignal;
    uintptr_t      alternativeStack;
    uintptr_t      handler;
    //TRACE("[signal] [queue]");
    
    assert(Thread != NULL);
    assert(Context != NULL);
    
    // Protect against signals received before the signal handler
    // has been installed
    handler = MemorySpaceSignalHandler(Thread->MemorySpace);
    if (!handler) {
        return;
    }

    while (1) {
        size_t bytesRead = streambuffer_stream_in(Thread->Signaling.Signals,
                                                  &threadSignal, sizeof(ThreadSignal_t), STREAMBUFFER_NO_BLOCK);
        if (!bytesRead) {
            break;
        }
        
        if (threadSignal.Flags & SIGNAL_SEPERATE_STACK) {
            // Missing implementation
            // AlternativeStack = Signal.Stack;
        }
        else {
            alternativeStack = 0;
        }
        
        ContextPushInterceptor(Context, alternativeStack, handler, threadSignal.Signal,
                               (uintptr_t)threadSignal.Argument, threadSignal.Flags);
        atomic_fetch_sub(&Thread->Signaling.Pending, 1);
    }
}

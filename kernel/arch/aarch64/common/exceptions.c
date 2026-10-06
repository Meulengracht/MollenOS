/**
 * Copyright, Philip Meulengracht
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
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include <arch/interrupts.h>
#include <arch/thread.h>
#include <arch/utils.h>
#include <component/cpu.h>
#include <debug.h>
#include <memoryspace.h>
#include <signal.h>
#include <string.h>
#include <threading.h>
#include "private.h"

extern Context_t* SyscallHandle(Context_t* context);

_Static_assert(sizeof(Context_t) == ARM64_CONTEXT_SIZE, "ARM64 exception frame size");
_Static_assert(offsetof(Context_t, Sp) == ARM64_CONTEXT_SP, "ARM64 exception SP offset");
_Static_assert(offsetof(Context_t, Pc) == ARM64_CONTEXT_PC, "ARM64 exception PC offset");
_Static_assert(offsetof(Context_t, Pstate) == ARM64_CONTEXT_PSTATE, "ARM64 exception PSTATE offset");
_Static_assert(offsetof(Context_t, UserSp) == ARM64_CONTEXT_USER_SP, "ARM64 exception user SP offset");
_Static_assert(offsetof(Context_t, FaultAddress) == ARM64_CONTEXT_FAULT, "ARM64 exception fault offset");
_Static_assert(offsetof(Context_t, ErrorCode) == ARM64_CONTEXT_SYNDROME, "ARM64 exception syndrome offset");

static int
__RestartMops(
    _InOut_ Context_t* context)
{
    uint64_t remaining;

    // ESR_EL1.EC[31:26]=0x27 identifies a MOPS operand-format exception,
    // not an ordinary translation fault. Its ISS fields describe the
    // interrupted sequence: [14:10] destination register, [9:5] source
    // register (or SET value), [4:0] size register, [24] SET versus CPY,
    // [18] epilogue versus main, [17] WrongOption, [16] OptionA.
    // Context.X holds saved x0..x30, and Context.Pc is the saved ELR_EL1.
    uint64_t     syndrome = context->ErrorCode;
    unsigned int destinationRegister = (syndrome >> ARM64_MOPS_DESTINATION_SHIFT) & ARM64_MOPS_REGISTER_MASK;
    unsigned int sourceRegister = (syndrome >> ARM64_MOPS_SOURCE_SHIFT) & ARM64_MOPS_REGISTER_MASK;
    unsigned int sizeRegister = syndrome & ARM64_MOPS_REGISTER_MASK;
    int          set = (syndrome & ARM64_MOPS_SET) != 0;
    
    // OptionA describes the expected format; WrongOption indicates that the
    // saved operands use the other format. XOR obtains their actual format.
    int      formatA = ((syndrome >> ARM64_MOPS_OPTION_SHIFT) ^
        (syndrome >> ARM64_MOPS_WRONG_OPTION_SHIFT)) & 1;

    // Encoding 31 is not an element of Context.X. Never index past x30;
    // for SET, the source field is a value operand and is not restored here.
    if (destinationRegister == ARM64_MOPS_REGISTER_MASK || sizeRegister == ARM64_MOPS_REGISTER_MASK) {
        return 0;
    }
    
    if (!set && sourceRegister == ARM64_MOPS_REGISTER_MASK) {
        return 0;
    }
    
    remaining = context->X[sizeRegister];
    // Restore ordinary starting pointers and a positive length before
    // retrying the prologue (Arm's generic MOPS recovery rules). Option A
    // forward operations encode a negative remaining size, with pointers
    // advanced to the end; adding it moves back to the uncopied range.
    // Option A backward CPY already has prologue-ready pointers/length.
    if (formatA && (set || (remaining & ARM64_SIGN_BIT))) {
        context->X[destinationRegister] += remaining;
        if (!set) {
            context->X[sourceRegister] += remaining;
        }
        context->X[sizeRegister] = -remaining;
    } else if (!set && !formatA && (context->Pstate & ARM64_PSTATE_NEGATIVE)) {
        // Saved PSTATE.N[31]=1 marks an Option B backward copy. Its positive
        // remaining length must be subtracted from both pointers. Option B
        // forward CPY and SET need no operand adjustment.
        context->X[destinationRegister] -= remaining;
        context->X[sourceRegister] -= remaining;
    }
    
    // Each instruction is 4 bytes: main is one instruction past prologue,
    // epilogue is two. Reexecute prologue with repaired operands, rather than
    // continuing a format selected by a different CPU before migration.
    context->Pc -= (syndrome & ARM64_MOPS_EPILOGUE) ?
        2 * ARM64_INSTRUCTION_SIZE : ARM64_INSTRUCTION_SIZE;
    return 1;
}

static enum OSPageFaultCode
__ResolvePageFault(
    _In_ Context_t* context,
    _In_ int        user)
{
    oserr_t      status;
    unsigned int attributes;
    unsigned int exceptionClass = context->ErrorCode >> ARM64_ESR_CLASS_SHIFT;
    unsigned int syndrome = context->ErrorCode & ARM64_ESR_SYNDROME_MASK;
    
    int instruction = exceptionClass == ARM64_EC_INSTRUCTION_ABORT_USER ||
        exceptionClass == ARM64_EC_INSTRUCTION_ABORT_KERNEL;

    if (context->FaultAddress < ARM64_PAGE_SIZE ||
        (syndrome & ARM64_ABORT_KIND_MASK) != ARM64_ABORT_TRANSLATION) {
        return OSPAGEFAULT_RESULT_FAULT;
    }
    
    if (user) {
        // An EL0 translation fault must not commit a privileged reservation.
        // Check the reserved PTE, which retains access rights before commit.
        if (context->FaultAddress >> ARM64_VIRTUAL_ADDRESS_BITS) {
            return OSPAGEFAULT_RESULT_FAULT;
        }
        
        status = GetMemorySpaceAttributes(
            GetCurrentMemorySpace(),
            context->FaultAddress,
            ARM64_PAGE_SIZE - (context->FaultAddress & (ARM64_PAGE_SIZE - 1)), 
            &attributes
        );
        if (status != OS_EOK || !(attributes & MAPPING_USERSPACE)) {
            return OSPAGEFAULT_RESULT_FAULT;
        }
        if (instruction && !(attributes & MAPPING_EXECUTABLE)) {
            return OSPAGEFAULT_RESULT_FAULT;
        }
        if (!instruction && (syndrome & ARM64_ABORT_WRITE) && (attributes & MAPPING_READONLY)) {
            return OSPAGEFAULT_RESULT_FAULT;
        }
    }
    return DebugPageFault(context, context->FaultAddress);
}

static Context_t*
__ReturnFromSignal(
    _InOut_ Context_t* context)
{
    Context_t restored;
    uintptr_t source = context->X[0];
    uintptr_t stack = context->Sp;
    oserr_t   status;

    // Copy through the shared userspace mapping lock and a kernel alias. This
    // also prevents concurrent unmap/protection changes during frame retrieval.
    status = MemorySpaceCopyUser((void*)source, &restored, sizeof(restored), false);
    if (status != OS_EOK) {
        return NULL;
    }
    
    if ((restored.Pc >> ARM64_VIRTUAL_ADDRESS_BITS) || (restored.Pc & (ARM64_INSTRUCTION_SIZE - 1))) {
        return NULL;
    }
    
    if ((restored.UserSp >> ARM64_VIRTUAL_ADDRESS_BITS) || (restored.UserSp & (ARM64_STACK_ALIGNMENT - 1))) {
        return NULL;
    }
    
    restored.Pstate &= ARM64_PSTATE_NZCV;
    restored.Sp = stack;
    *context = restored;
    return context;
}

static int
__HandleSupervisorCall(
    _InOut_ Context_t*   context,
    _In_    unsigned int syndrome,
    _In_    int          user,
    _Out_   Context_t**  contextOut)
{
    Context_t*   restored;
    unsigned int immediate = syndrome & ARM64_SVC_IMMEDIATE_MASK;

    // Only userspace uses this request to call a kernel service. A service may
    // block, so allow timer and device interrupts while it runs. Mask them
    // again before the exception return code restores the selected context.
    if (user && immediate == ARM64_SVC_SYSCALL) {
        InterruptEnable();
        *contextOut = SyscallHandle(context);
        InterruptDisable();
        return 1;
    }
    
    // This is the kernel's internal request to give another thread a turn,
    // not a userspace service. Use the same bookkeeping as a timer interrupt
    // so the scheduler can save this thread and select the context to resume.
    // Keep interrupts masked while that selection is in progress.
    if (!user && immediate == ARM64_SVC_YIELD) {
        CpuCoreEnterInterrupt(context, 0);
        Arm64TimerAdvance(0);
        *contextOut = CpuCoreExitInterrupt(context, 0);
        return 1;
    }
    
    // A userspace signal handler asks to resume the state saved before the
    // signal. That frame comes from user memory and must be validated. Reading
    // it may wait for another CPU to finish changing the process's mappings,
    // so allow interrupts during the copy, then mask them before returning.
    if (user && immediate == ARM64_SVC_SIGNAL_RETURN) {
        InterruptEnable();
        restored = __ReturnFromSignal(context);
        InterruptDisable();
        // Resume only a successfully copied and validated frame. Otherwise,
        // leave this request unhandled so exception dispatch reports the fault.
        if (restored) {
            *contextOut = restored;
            return 1;
        }
    }
    return 0;
}

static int
__IsAbort(
    _In_ unsigned int exceptionClass)
{
    return exceptionClass == ARM64_EC_INSTRUCTION_ABORT_USER ||
        exceptionClass == ARM64_EC_INSTRUCTION_ABORT_KERNEL ||
        exceptionClass == ARM64_EC_DATA_ABORT_USER ||
        exceptionClass == ARM64_EC_DATA_ABORT_KERNEL;
}

static int
__SignalForException(
    _In_ unsigned int exceptionClass)
{
    switch (exceptionClass) {
        case ARM64_EC_FP_EXCEPTION:
            return SIGFPE;
        case ARM64_EC_BREAKPOINT:
        case ARM64_EC_SOFTWARE_STEP:
        case ARM64_EC_BRK:
            return SIGTRAP;
        case ARM64_EC_UNKNOWN:
        case ARM64_EC_FP_ACCESS:
            return SIGILL;
        default:
            return SIGSEGV;
    }
}

Context_t*
Arm64HandleException(
    _InOut_ Context_t* context)
{
    unsigned int         exceptionClass = context->ErrorCode >> ARM64_ESR_CLASS_SHIFT;
    unsigned int         syndrome = context->ErrorCode & ARM64_ESR_SYNDROME_MASK;
    int                  user = (context->Pstate & ARM64_PSTATE_MODE_MASK) == ARM64_PSTATE_EL0T;
    int                  abort = __IsAbort(exceptionClass);
    int                  restarted;
    int                  handled;
    enum OSPageFaultCode result = OSPAGEFAULT_RESULT_FAULT;
    Context_t*           restored;

    // Kernel memcpy stays C-only, so MOPS recovery is userspace-only.
    if (exceptionClass == ARM64_EC_MOPS && user) {
        restarted = __RestartMops(context);
        if (restarted) {
            return context;
        }
    }
    if (exceptionClass == ARM64_EC_SVC) {
        // Whether an SVC was handled is independent of the callback's context
        // pointer. Preserve that pointer exactly rather than falling through.
        handled = __HandleSupervisorCall(context, syndrome, user, &restored);
        if (handled) {
            return restored;
        }
    }

    if (abort && CpuCoreCurrentThread(CpuCoreCurrent())) {
        result = __ResolvePageFault(context, user);
        if (result == OSPAGEFAULT_RESULT_MAPPED) {
            return context;
        }
    }
    
    if (user) {
        if (abort) {
            SignalExecuteLocalThreadTrap(context, SIGSEGV, SIGNAL_FLAG_PAGEFAULT,
                (void*)context->FaultAddress, (void*)result);
        } else {
            SignalExecuteLocalThreadTrap(context, __SignalForException(exceptionClass), 0, NULL, NULL);
        }
        return context;
    }
    
    DebugPanic(FATAL_SCOPE_KERNEL, context, "ARM64 exception ESR=%llx FAR=%llx PC=%llx",
        context->ErrorCode, context->FaultAddress, context->Pc);
    ArchProcessorHalt();
    return context;
}

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
 * Interrupt Interface
 * - Contains the shared kernel interrupt interface
 *   that all sub-layers must conform to
 *
 * - ISA Interrupts should be routed to boot-processor without lowest-prio?
 */
#define __MODULE        "IRQS"
//#define __TRACE

#include <modules/manager.h>
#include <arch/utils.h>
#include <memoryspace.h>
#include <threading.h>
#include <assert.h>
#include <debug.h>
#include <arch.h>

extern OsStatus_t ThreadingFpuException(MCoreThread_t *Thread);
extern OsStatus_t GetVirtualPageAttributes(SystemMemorySpace_t*, VirtualAddress_t, Flags_t*);
extern reg_t __getcr2(void);

void
ExceptionEntry(
    _In_ Context_t* Registers)
{
    MCoreThread_t*Thread = NULL;
    uintptr_t Address    = __MASK;
    int IssueFixed       = 0;

    // Handle IRQ
    if (Registers->Irq == 0) {      // Divide By Zero (Non-math instruction)
        if (SignalCreateInternal(Registers, SIGFPE) == OsSuccess) {
            IssueFixed = 1;
        }
    }
    else if (Registers->Irq == 1) { // Single Step
        if (DebugSingleStep(Registers) == OsSuccess) {
            // Re-enable single-step
        }
        IssueFixed = 1;
    }
    else if (Registers->Irq == 2) { // NMI
        // Fall-through to kernel fault
    }
    else if (Registers->Irq == 3) { // Breakpoint
        DebugBreakpoint(Registers);
        IssueFixed = 1;
    }
    else if (Registers->Irq == 4) { // Overflow
        if (SignalCreateInternal(Registers, SIGSEGV) == OsSuccess) {
            IssueFixed = 1;
        }
    }
    else if (Registers->Irq == 5) { // Bound Range Exceeded
        if (SignalCreateInternal(Registers, SIGSEGV) == OsSuccess) {
            IssueFixed = 1;
        }
    }
    else if (Registers->Irq == 6) { // Invalid Opcode
        if (SignalCreateInternal(Registers, SIGILL) == OsSuccess) {
            IssueFixed = 1;
        }
    }
    else if (Registers->Irq == 7) { // DeviceNotAvailable 
        Thread = GetCurrentThreadForCore(ArchGetProcessorCoreId());
        assert(Thread != NULL);

        // This might be because we need to restore fpu/sse state
        if (ThreadingFpuException(Thread) != OsSuccess) {
            if (SignalCreateInternal(Registers, SIGFPE) == OsSuccess) {
                IssueFixed = 1;
            }
        }
        else {
            IssueFixed = 1;
        }
    }
    else if (Registers->Irq == 8) { // Double Fault
        // Fall-through to kernel fault
    }
    else if (Registers->Irq == 9) { // Coprocessor Segment Overrun (Obsolete)
        // Fall-through to kernel fault
    }
    else if (Registers->Irq == 10) { // Invalid TSS
        // Fall-through to kernel fault
    }
    else if (Registers->Irq == 11) { // Segment Not Present
        if (SignalCreateInternal(Registers, SIGSEGV) == OsSuccess) {
            IssueFixed = 1;
        }
    }
    else if (Registers->Irq == 12) { // Stack Segment Fault
        if (SignalCreateInternal(Registers, SIGSEGV) == OsSuccess) {
            IssueFixed = 1;
        }
    }
    else if (Registers->Irq == 13) { // General Protection Fault
        if (SignalCreateInternal(Registers, SIGSEGV) == OsSuccess) {
            IssueFixed = 1;
        }
    }
    else if (Registers->Irq == 14) {    // Page Fault
        Address = (uintptr_t)__getcr2();

        // The first thing we must check before propegating events
        // is that we must check special locations
        if (Address == MEMORY_LOCATION_SIGNAL_RET) {
            TerminateThread(GetCurrentThreadId(), SIGSEGV, 1);
            ERROR(" >> return from signal detected");
            for(;;);
        }

        // Final step is to see if kernel can handle the unallocated address
        if (DebugPageFault(Registers, Address) == OsSuccess) {
            IssueFixed = 1;
        }
        else {
            ERROR("%s: MEMORY_ACCESS_FAULT: 0x%" PRIxIN ", 0x%" PRIxIN ", 0x%" PRIxIN "", 
                GetCurrentThreadForCore(ArchGetProcessorCoreId())->Name, 
                Address, Registers->ErrorCode, CONTEXT_IP(Registers));
            if (SignalCreateInternal(Registers, SIGSEGV) == OsSuccess) {
                IssueFixed = 1;
            }
        }
    }
    else if (Registers->Irq == 16 || Registers->Irq == 19) {    // FPU & SIMD Floating Point Exception
        if (SignalCreateInternal(Registers, SIGFPE) == OsSuccess) {
            IssueFixed = 1;
        }
    }

    // Was the exception handled?
    if (IssueFixed == 0) {
        uintptr_t Base  = 0;
        char *Name      = NULL;
        LogSetRenderMode(1);

        // Was it a page-fault?
        if (Address != __MASK) {
            // Bit 1 - present status 
            // Bit 2 - write access
            // Bit 4 - user/kernel
            WRITELINE("page-fault address: 0x%" PRIxIN ", error-code 0x%" PRIxIN "", Address, Registers->ErrorCode);
            WRITELINE("existing mapping for address: 0x%" PRIxIN "", GetMemorySpaceMapping(GetCurrentMemorySpace(), Address));
            WRITELINE("existing attribs for address: 0x%" PRIxIN "", GetMemorySpaceAttributes(GetCurrentMemorySpace(), Address));
        }

        // Locate which module
        if (DebugGetModuleByAddress(GetCurrentModule(), CONTEXT_IP(Registers), &Base, &Name) == OsSuccess) {
            uintptr_t Diff = CONTEXT_IP(Registers) - Base;
            WRITELINE("Faulty Address: 0x%" PRIxIN " (%s)", Diff, Name);
        }
        else {
            WRITELINE("Faulty Address: 0x%" PRIxIN "", CONTEXT_IP(Registers));
        }

        // Enter panic handler
        ArchDumpThreadContext(Registers);
        DebugPanic(FATAL_SCOPE_KERNEL, Registers, __MODULE,
            "Unhandled or fatal interrupt %" PRIuIN ", Error Code: %" PRIuIN ", Faulty Address: 0x%" PRIxIN "",
            Registers->Irq, Registers->ErrorCode, CONTEXT_IP(Registers));
    }
}

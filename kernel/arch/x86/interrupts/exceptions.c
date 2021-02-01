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
 * Interrupt Interface
 * - Contains the shared kernel interrupt interface
 *   that all sub-layers must conform to
 *
 * - ISA Interrupts should be routed to boot-processor without lowest-prio?
 */
#define __MODULE "IRQS"
//#define __TRACE

#include <arch.h>
#include <arch/utils.h>
#include <assert.h>
#include <component/cpu.h>
#include <debug.h>
#include <interrupts.h>
#include <modules/manager.h>
#include <memoryspace.h>
#include <threading.h>

#define PAGE_FAULT_PRESENT 0x1
#define PAGE_FAULT_WRITE   0x2
#define PAGE_FAULT_USER    0x4

extern OsStatus_t ThreadingFpuException(Thread_t *Thread);
extern reg_t      __getcr2(void);

static void
HardFault(
    _In_ Context_t* Registers,
    _In_ uintptr_t  PFAddress)
{
    uintptr_t Base  = 0;
    char *Name      = NULL;
    LogSetRenderMode(1);

    // Was it a page-fault?
    if (PFAddress != __MASK) {
        // Bit 1 - present status 
        // Bit 2 - write access
        // Bit 4 - user/kernel
        WRITELINE("page-fault address: 0x%" PRIxIN ", error-code 0x%" PRIxIN "", PFAddress, Registers->ErrorCode);
        if (GetMemorySpaceMapping(GetCurrentMemorySpace(), PFAddress, 1, &Base) == OsSuccess) {
            WRITELINE("existing mapping for address: 0x%" PRIxIN "", Base);
            WRITELINE("existing attribs for address: 0x%" PRIxIN "", GetMemorySpaceAttributes(GetCurrentMemorySpace(), PFAddress));
        }
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
    WRITELINE("Unhandled or fatal interrupt %" PRIuIN ", Error Code: %" PRIuIN ", Faulty Address: 0x%" PRIxIN "",
        Registers->Irq, Registers->ErrorCode, CONTEXT_IP(Registers));
    if (Registers) {
        __asm { xchg bx, bx };
        return;
    }
    DebugPanic(FATAL_SCOPE_KERNEL, Registers, __MODULE,
        "Unhandled or fatal interrupt %" PRIuIN ", Error Code: %" PRIuIN ", Faulty Address: 0x%" PRIxIN "",
        Registers->Irq, Registers->ErrorCode, CONTEXT_IP(Registers));
}

void
ExceptionEntry(
    _In_ Context_t* Registers)
{
    uintptr_t        address    = __MASK;
    int              issueFixed = 0;
    SystemCpuCore_t* core;
    Thread_t*        thread;

    // Handle IRQ
    if (Registers->Irq == 0) {      // Divide By Zero (Non-math instruction)
        SignalExecuteLocalThreadTrap(Registers, SIGFPE, NULL);
        issueFixed = 1;
    }
    else if (Registers->Irq == 1) { // Single Step
        if (DebugSingleStep(Registers) == OsSuccess) {
            // Re-enable single-step
        }
        issueFixed = 1;
    }
    else if (Registers->Irq == 2) { // NMI
        // Fall-through to kernel fault
    }
    else if (Registers->Irq == 3) { // Breakpoint
        DebugBreakpoint(Registers);
        issueFixed = 1;
    }
    else if (Registers->Irq == 4) { // Overflow
        SignalExecuteLocalThreadTrap(Registers, SIGSEGV, NULL);
        issueFixed = 1;
    }
    else if (Registers->Irq == 5) { // Bound Range Exceeded
        SignalExecuteLocalThreadTrap(Registers, SIGSEGV, NULL);
        issueFixed = 1;
    }
    else if (Registers->Irq == 6) { // Invalid Opcode
        SignalExecuteLocalThreadTrap(Registers, SIGILL, NULL);
        issueFixed = 1;
    }
    else if (Registers->Irq == 7) { // DeviceNotAvailable 
        // This might be because we need to restore fpu/sse state
        core = CpuCoreCurrent();
        thread = CpuCoreCurrentThread(core);

        assert(thread != NULL);

        if (ThreadingFpuException(thread) != OsSuccess) {
            SignalExecuteLocalThreadTrap(Registers, SIGFPE, NULL);
        }
        issueFixed = 1;
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
        SignalExecuteLocalThreadTrap(Registers, SIGSEGV, NULL);
        issueFixed = 1;
    }
    else if (Registers->Irq == 12) { // Stack Segment Fault
        SignalExecuteLocalThreadTrap(Registers, SIGSEGV, NULL);
        issueFixed = 1;
    }
    else if (Registers->Irq == 13) { // General Protection Fault
        core   = CpuCoreCurrent();
        thread = CpuCoreCurrentThread(core);

        ERROR("%s: FAULT: 0x%" PRIxIN ", 0x%" PRIxIN "",
              thread != NULL ? ThreadName(thread) : "Null",
              Registers->ErrorCode, CONTEXT_IP(Registers));
        if (core) {
            __asm { xchg bx, bx };
            return;
        }
        SignalExecuteLocalThreadTrap(Registers, SIGSEGV, NULL);
        issueFixed = 1;
    }
    else if (Registers->Irq == 14) {    // Page Fault
        address = (uintptr_t)__getcr2();
        core    = CpuCoreCurrent();
        thread  = CpuCoreCurrentThread(core);

        // Debug the error code
        if (Registers->ErrorCode & PAGE_FAULT_PRESENT) {
            // Page access violation for a page that was present
            unsigned int attributes             = GetMemorySpaceAttributes(GetCurrentMemorySpace(), address);
            int          invalidProtectionLevel = (Registers->ErrorCode & PAGE_FAULT_USER)
                    && (attributes & MAPPING_USERSPACE) == 0;
            
            if (invalidProtectionLevel) {
                ERROR("%s: ACCESS_VIOLATION: 0x%" PRIxIN ", 0x%" PRIxIN ", 0x%" PRIxIN "",
                      thread != NULL ? ThreadName(thread) : "Null",
                      address, Registers->ErrorCode, CONTEXT_IP(Registers));
                if (Registers->ErrorCode & PAGE_FAULT_USER) {
                    SignalExecuteLocalThreadTrap(Registers, SIGSEGV, NULL);
                    issueFixed = 1;
                }
            }
            else if (Registers->ErrorCode & PAGE_FAULT_WRITE) {
                // Write access, so lets verify that write attributes are set, if they
                // are not, then the thread tried to write to read-only memory
                if (attributes & MAPPING_READONLY) {
                    // If it was a user-process, kill it, otherwise fall through to kernel crash
                    ERROR("%s: WRITE_ACCESS_VIOLATION: 0x%" PRIxIN ", 0x%" PRIxIN ", 0x%" PRIxIN "",
                          thread != NULL ? ThreadName(thread) : "Null",
                          address, Registers->ErrorCode, CONTEXT_IP(Registers));
                    if (Registers->ErrorCode & PAGE_FAULT_USER) {
                        SignalExecuteLocalThreadTrap(Registers, SIGSEGV, NULL);
                        issueFixed = 1;
                    }
                }
                else {
                    // Invalidate the address and return
                    CpuInvalidateMemoryCache((void*)address, GetMemorySpacePageSize());
                    issueFixed = 1;
                }
            }
            else {
                // Read access violation, but this kernel does not map pages without
                // read access, so something terrible has happened. Fall through to kernel crash
                ERROR("%s: READ_ACCESS_VIOLATION: 0x%" PRIxIN ", 0x%" PRIxIN ", 0x%" PRIxIN "",
                      thread != NULL ? ThreadName(thread) : "Null",
                      address, Registers->ErrorCode, CONTEXT_IP(Registers));
            }
        }
        else {
            // Page was not present, this could be because of lazy-comitting, lets try
            // to fix it by comitting the address. 
            if (address > 0x1000 && DebugPageFault(Registers, address) == OsSuccess) {
                issueFixed = 1;
            }
            else {
                ERROR("%s: ADDRESS_VIOLATION: 0x%" PRIxIN ", 0x%" PRIxIN ", 0x%" PRIxIN "",
                      thread != NULL ? ThreadName(thread) : "Null",
                      address, Registers->ErrorCode, CONTEXT_IP(Registers));
                SignalExecuteLocalThreadTrap(Registers, SIGSEGV, NULL);
                issueFixed = 1;
            }
        }


    }
    else if (Registers->Irq == 16 || Registers->Irq == 19) {    // FPU & SIMD Floating Point Exception
        SignalExecuteLocalThreadTrap(Registers, SIGFPE, NULL);
        issueFixed = 1;
    }
    
    // Was the exception handled?
    if (!issueFixed) {
        HardFault(Registers, address);
    }
}

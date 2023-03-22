/**
 * Copyright 2023, Philip Meulengracht
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
 */

//#define __TRACE

#include <arch/thread.h>
#include <arch/utils.h>
#include <arch/x86/memory.h>
#include <assert.h>
#include <component/cpu.h>
#include <debug.h>
#include <ddk/ddkdefs.h>
#include <memoryspace.h>
#include <threading.h>

#define PAGE_FAULT_PRESENT 0x1
#define PAGE_FAULT_WRITE   0x2
#define PAGE_FAULT_USER    0x4

extern oserr_t ThreadingFpuException(Thread_t *thread);
extern reg_t   __getcr2(void);

static void
HardFault(
    _In_ Context_t*  context,
    _In_ const char* exception,
    _In_ uintptr_t   pfAddress)
{
    uintptr_t    physicalBase = 0;
    unsigned int attributes   = 0;
    LogSetRenderMode(1);

    ERROR(
            "%s ocurred, error-code: %" PRIuIN ", at address: 0x%" PRIxIN "",
            exception, context->ErrorCode, CONTEXT_IP(context)
    );

    // Was it a page-fault?
    if (context->Irq == 14) {
        // Bit 1 - present status 
        // Bit 2 - write access
        // Bit 4 - user/kernel
        DEBUG("address which could not be accessed: 0x%" PRIxIN "", pfAddress);
        if (GetMemorySpaceMapping(GetCurrentMemorySpace(), pfAddress, 1, &physicalBase) == OS_EOK) {
            GetMemorySpaceAttributes(GetCurrentMemorySpace(), pfAddress, PAGE_SIZE, &attributes);
            DEBUG("existing mapping for address: 0x%" PRIxIN "", physicalBase);
            DEBUG("existing attribs for address: 0x%" PRIxIN "", attributes);
        }
    }
    ArchThreadContextDump(context);
    DebugPanic(FATAL_SCOPE_KERNEL, context, "FATAL EXCEPTION OCCURRED");
}

static bool
__HandleDivideByZero(
        _In_ Context_t* context)
{
    SignalExecuteLocalThreadTrap(context, SIGFPE, 0, NULL, NULL);
    return true;
}

static bool
__HandleSingleStep(
        _In_ Context_t* context)
{
    if (DebugSingleStep(context) == OS_EOK) {
        // Re-enable single-step
    }
    return true;
}

static bool
__HandleBreakpoint(
        _In_ Context_t* context)
{
    DebugBreakpoint(context);
    return true;
}

static bool
__HandleOverflow(
        _In_ Context_t* context)
{
    SignalExecuteLocalThreadTrap(context, SIGSEGV, 0, NULL, NULL);
    return true;
}

static bool
__HandleBoundRangeExceeded(
        _In_ Context_t* context)
{
    SignalExecuteLocalThreadTrap(context, SIGSEGV, 0, NULL, NULL);
    return true;
}

static bool
__HandleIllegalOpcode(
        _In_ Context_t* context)
{
    SignalExecuteLocalThreadTrap(context, SIGILL, 0, NULL, NULL);
    return true;
}

static bool
__HandleDeviceNotAvailable(
        _In_ Context_t* context)
{
    // This might be because we need to restore fpu/sse state
    Thread_t* thread = CpuCoreCurrentThread(CpuCoreCurrent());
    if (thread == NULL) {
        return false;
    }

    if (ThreadingFpuException(thread) != OS_EOK) {
        SignalExecuteLocalThreadTrap(context, SIGFPE, 0, NULL, NULL);
    }
    return true;
}

static bool
__HandleSegmentNotPresent(
        _In_ Context_t* context)
{
    SignalExecuteLocalThreadTrap(context, SIGSEGV, 0, NULL, NULL);
    return true;
}

static bool
__HandleStackSegmentFault(
        _In_ Context_t* context)
{
    SignalExecuteLocalThreadTrap(context, SIGSEGV, 0, NULL, NULL);
    return true;
}

static bool
__HandleGeneralProtectionFault(
        _In_ Context_t* context)
{
    Thread_t* thread = CpuCoreCurrentThread(CpuCoreCurrent());
    if (thread == NULL) {
        return false;
    }

    ERROR("%s: FAULT: 0x%" PRIxIN ", 0x%" PRIxIN ", 0x%" PRIxIN,
          thread != NULL ? ThreadName(thread) : "Null",
          context->ErrorCode, CONTEXT_IP(context), CONTEXT_SP(context));
    SignalExecuteLocalThreadTrap(context, SIGSEGV, 0, NULL, NULL);
    return true;
}

static bool
__HandlePageFault(
        _In_ Context_t* context)
{
    uintptr_t address = (uintptr_t)__getcr2();
    Thread_t* thread  = CpuCoreCurrentThread(CpuCoreCurrent());
    if (thread == NULL) {
        return false;
    }

    // Debug the error code
    if (context->ErrorCode & PAGE_FAULT_PRESENT) {
        // Page access violation for a page that was present
        unsigned int attributes = 0;
        int          invalidProtectionLevel;

        GetMemorySpaceAttributes(GetCurrentMemorySpace(), address, PAGE_SIZE, &attributes);
        invalidProtectionLevel = (context->ErrorCode & PAGE_FAULT_USER) && (attributes & MAPPING_USERSPACE) == 0;

        if (invalidProtectionLevel) {
            ERROR("%s: ACCESS_VIOLATION: 0x%" PRIxIN ", 0x%" PRIxIN ", 0x%" PRIxIN "",
                  thread != NULL ? ThreadName(thread) : "Null",
                  address, context->ErrorCode, CONTEXT_IP(context));
            if (context->ErrorCode & PAGE_FAULT_USER) {
                SignalExecuteLocalThreadTrap(context, SIGSEGV, SIGNAL_FLAG_PAGEFAULT, (void*)address, NULL);
                return true;
            }
        } else if (context->ErrorCode & PAGE_FAULT_WRITE) {
            // Write access, so lets verify that write attributes are set, if they
            // are not, then the thread tried to write to read-only memory
            if (attributes & MAPPING_READONLY) {
                // If it was a user-process, kill it, otherwise fall through to kernel crash
                ERROR("%s: WRITE_ACCESS_VIOLATION: 0x%" PRIxIN ", 0x%" PRIxIN ", 0x%" PRIxIN "",
                      thread != NULL ? ThreadName(thread) : "Null",
                      address, context->ErrorCode, CONTEXT_IP(context));
                if (context->ErrorCode & PAGE_FAULT_USER) {
                    SignalExecuteLocalThreadTrap(context, SIGSEGV, SIGNAL_FLAG_PAGEFAULT, (void*)address, NULL);
                    return true;
                }
            } else {
                // Invalidate the address and return
                CpuInvalidateMemoryCache((void*)address, GetMemorySpacePageSize());
                return true;
            }
        } else {
            // Read access violation, but this kernel does not map pages without
            // read access, so something terrible has happened. Fall through to kernel crash
            ERROR("%s: READ_ACCESS_VIOLATION: 0x%" PRIxIN ", 0x%" PRIxIN ", 0x%" PRIxIN "",
                  thread != NULL ? ThreadName(thread) : "Null",
                  address, context->ErrorCode, CONTEXT_IP(context));
        }
    } else {
        // Page was not present, this could be because of lazy-comitting, lets try
        // to fix it by comitting the address.
        if (address < 0x1000) {
            // The first page of memory is left as a NULL catcher, in this case there is no fix and we
            // should only attempt to fix this by executing a trap
            SignalExecuteLocalThreadTrap(context, SIGSEGV, SIGNAL_FLAG_PAGEFAULT, (void*)address, (void*)OSPAGEFAULT_RESULT_FAULT);
        } else {
            enum OSPageFaultCode result = DebugPageFault(context, address);
            if (result != OSPAGEFAULT_RESULT_MAPPED) {
                SignalExecuteLocalThreadTrap(context, SIGSEGV, SIGNAL_FLAG_PAGEFAULT, (void*)address, (void*)result);
            }
        }
        return true;
    }
    return false;
}

static bool
__HandleFPUException(
        _In_ Context_t* context)
{
    SignalExecuteLocalThreadTrap(context, SIGFPE, 0, NULL, NULL);
    return true;
}

static bool
__HandleSIMDException(
        _In_ Context_t* context)
{
    SignalExecuteLocalThreadTrap(context, SIGFPE, 0, NULL, NULL);
    return true;
}

#define __XHANDLER_COUNT 20U

static const char* g_unknownStr = "unknown exception";
static struct {
    bool       (*handler)(Context_t*);
    const char* description;
} g_handlers[__XHANDLER_COUNT] = {
        { __HandleDivideByZero, "divison by zero exception" },
        { __HandleSingleStep, "single-step exception" },
        { NULL, "nmi exception" },
        { __HandleBreakpoint, "breakpoint exception" },
        { __HandleOverflow, "overflow exception" },
        { __HandleBoundRangeExceeded, "bound-range exceeded exception" },
        { __HandleIllegalOpcode, "illegal opcode exception" },
        { __HandleDeviceNotAvailable, "device not available exception" },
        { NULL, "double-fault exception" },
        { NULL, "coprocessor segment overrun exception" },
        { NULL, "invalid tss exception" },
        { __HandleSegmentNotPresent, "segment not present exception" },
        { __HandleStackSegmentFault, "stack segment exception" },
        { __HandleGeneralProtectionFault, "general protection exception" },
        { __HandlePageFault, "page fault exception" },
        { NULL, "unknown exception" },
        { __HandleFPUException, "fpu floating point exception" },
        { NULL, "unknown exception" },
        { NULL, "unknown exception" },
        { __HandleSIMDException, "simd floating point exception" },
};

void
ExceptionEntry(
    _In_ Context_t* context)
{
    const char* description = g_unknownStr;

    if (context->Irq < __XHANDLER_COUNT) {
        if (g_handlers[context->Irq].handler != NULL) {
            if (g_handlers[context->Irq].handler(context)) {
                return;
            }
        }
        description = g_handlers[context->Irq].description;
    }
    HardFault(context, description, (uintptr_t)__getcr2());
}

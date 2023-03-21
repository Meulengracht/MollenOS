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

#define __MODULE "DBGI"
//#define __TRACE

#include <arch/thread.h>
#include <arch/utils.h>
#include <debug.h>
#include <memoryspace.h>
#include <machine.h>
#include <shm.h>
#include <stdio.h>
#include <string.h>

oserr_t
DebugSingleStep(
    _In_ Context_t* Context)
{
    TRACE("DebugSingleStep(IP 0x%" PRIxIN ")", CONTEXT_IP(Context));
    _CRT_UNUSED(Context);
    return OS_EOK;
}

oserr_t
DebugBreakpoint(
    _In_ Context_t* Context)
{
    TRACE("DebugBreakpoint(IP 0x%" PRIxIN ")", CONTEXT_IP(Context));
    _CRT_UNUSED(Context);
    return OS_EOK;
}

static oserr_t
__MapFaultingAddress(
        _In_ OSMemoryDescriptor_t* descriptor,
        _In_ uintptr_t             address)
{
    MemorySpace_t* memorySpace = GetCurrentMemorySpace();
    size_t         pageSize = GetMemorySpacePageSize();
    oserr_t        oserr;

    // Otherwise, commit the page and continue without anyone noticing, and handle race-conditions
    // if two different threads have accessed a reserved page. OsExists will be returned.
    if (descriptor->SHMTag != UUID_INVALID) {
        oserr = SHMCommit(
                descriptor->SHMTag,
                (void*)descriptor->StartAddress,
                (void*)address,
                pageSize
        );
    } else {
        uintptr_t physicalAddress;
        oserr = MemorySpaceCommit(
                memorySpace,
                address,
                &physicalAddress,
                pageSize,
                0,
                0
        );
    }
    if (oserr == OS_EOK) {
        // If the mapping has the attribute CLEAN, then we zero each
        // allocated page.
        if (descriptor->Attributes & MAPPING_CLEAN) {
            memset((void*)(address & (pageSize - 1)), 0, pageSize);
        }
    } else if (oserr == OS_EEXISTS) {
        oserr = OS_EOK;
    }
    return oserr;
}

static enum OSPageFaultCode
__HandleUserspaceFault(
        _In_ OSMemoryDescriptor_t* descriptor,
        _In_ uintptr_t             address)
{
    enum OSPageFaultCode result = OSPAGEFAULT_RESULT_MAPPED;

    // In case of a freed part of the allocation, do not attempt mapping
    // that part
    if (descriptor->AllocationSize == 0) {
        return OSPAGEFAULT_RESULT_FAULT;
    }

    // userspace allocation, perform additional checks.
    // 1) Was a guard page hit?
    // 2) Should the exception be propegated (i.e. memory handlers) instead
    //    of handled here? Return an error in this case.
    if (descriptor->Attributes & MAPPING_STACK) {
        // Detect stack overflow. If the guard page was hit, we've exceeded
        // allocation size set in descriptor.AllocationSize. If the guard page
        // was hit, we've beyound bounds.
        if (address < descriptor->StartAddress) {
            // Guard page was hit, abort
            return OSPAGEFAULT_RESULT_OVERFLOW;
        }
    } else if (descriptor->Attributes & MAPPING_TRAPPAGE) {
        TRACE("DebugPageFault trappage hit 0x%" PRIxIN, address);
        result = OSPAGEFAULT_RESULT_TRAP;
    }

    if (__MapFaultingAddress(descriptor, address) != OS_EOK) {
        result = OSPAGEFAULT_RESULT_FAULT;
    }
    return result;
}

enum OSPageFaultCode
DebugPageFault(
    _In_ Context_t* context,
    _In_ uintptr_t  address)
{
    enum OSPageFaultCode result = OSPAGEFAULT_RESULT_MAPPED;
    OSMemoryDescriptor_t descriptor  = { .SHMTag = UUID_INVALID };
    MemorySpace_t*       memorySpace = GetCurrentMemorySpace();
    oserr_t              oserr;
    TRACE("DebugPageFault(context->ip=0x%" PRIxIN ", address=0x%" PRIxIN ")", CONTEXT_IP(context), address);

    // get information about the allocation first, if this succeeds then
    // the allocation was done by userspace
    oserr = MemorySpaceQuery(memorySpace, address, &descriptor);
    if (oserr == OS_EOK) {
        return __HandleUserspaceFault(&descriptor, address);
    }

    oserr = __MapFaultingAddress(&descriptor, address);
    if (oserr != OS_EOK) {
        result = OSPAGEFAULT_RESULT_FAULT;
    }
    return result;
}

static oserr_t
DebugHaltAllProcessorCores(
        _In_ uuid_t         ExcludeId,
        _In_ SystemCpu_t*   Processor)
{
    SystemCpuCore_t* i;

    i = Processor->Cores;
    while (i) {
        uuid_t coreID = CpuCoreId(i);
        if (coreID == ExcludeId) {
            i = CpuCoreNext(i);
            continue;
        }
        
        if (CpuCoreState(i) & CpuStateRunning) {
            oserr_t oserr = TxuMessageSend(
                    coreID,
                    CpuFunctionHalt,
                    NULL,
                    NULL,
                    1
            );
            if (oserr != OS_EOK) {
                WARNING("DebugHaltAllProcessorCores failed to halt core %u", coreID);
            }
        }
        i = CpuCoreNext(i);
    }
    return OS_EOK;
}

void
DebugPanic(
    _In_ int         FatalityScope,
    _In_ Context_t*  Context,
    _In_ const char* Message, ...)
{
    Thread_t* currentThread;
    char      messageBuffer[256];
    va_list   arguments;
    uuid_t    coreID;

    ERROR("DebugPanic(Scope %" PRIiIN ")", FatalityScope);

    // Disable all other cores in system if the fault is kernel scope
    coreID = ArchGetProcessorCoreId();
    if (FatalityScope == FATAL_SCOPE_KERNEL) {
        if (list_count(&GetMachine()->SystemDomains) != 0) {
            foreach(i, &GetMachine()->SystemDomains) {
                SystemDomain_t* Domain = (SystemDomain_t*)i->value;
                DebugHaltAllProcessorCores(coreID, &Domain->CoreGroup);
            }
        }
        else {
            DebugHaltAllProcessorCores(coreID, &GetMachine()->Processor);
        }
    }

    // Format the debug information
    va_start(arguments, Message);
    vsprintf(&messageBuffer[0], Message, arguments);
    va_end(arguments);
    LogSetRenderMode(1);
    LogAppendMessage(OSSYSLOGLEVEL_ERROR, &messageBuffer[0]);
    
    // Log cpu and threads
    currentThread = ThreadCurrentForCore(coreID);
    if (currentThread != NULL) {
        LogAppendMessage(
                OSSYSLOGLEVEL_ERROR,
                "Thread %s - %" PRIuIN " (Core %" PRIuIN ")!",
                ThreadName(currentThread),
                ThreadHandle(currentThread),
                coreID
        );
    }
    
    if (Context) {
        ArchThreadContextDump(Context);
        DebugStackTrace(Context, 8);
    }

    // Handle based on the scope of the fatality
    if (FatalityScope == FATAL_SCOPE_KERNEL) {
        ArchProcessorHalt();
    } else if (FatalityScope == FATAL_SCOPE_PROCESS) {
        // @todo
    } else if (FatalityScope == FATAL_SCOPE_THREAD) {
        // @todo
    } else {
        ERROR("Encounted an unkown fatality scope %" PRIiIN "", FatalityScope);
        ArchProcessorHalt();
    }
    for(;;);
}

oserr_t
DebugStackTrace(
    _In_ Context_t* context,
    _In_ size_t     maxFrames)
{
    // Derive stack pointer from the argument
    uintptr_t* stackPtr;
    uintptr_t  stackTop;
    uintptr_t  pageMask = ~(GetMemorySpacePageSize() - 1);
    size_t     i        = maxFrames;

    // Use local or given?
    if (context == NULL) {
        stackPtr = (uintptr_t*)&maxFrames;
        stackTop = ((uintptr_t)stackPtr & pageMask) + GetMemorySpacePageSize();
    } else if (IS_USER_STACK(&GetMachine()->MemoryMap, CONTEXT_USERSP(context))) {
        stackPtr = (uintptr_t*)CONTEXT_USERSP(context);
        stackTop = (CONTEXT_USERSP(context) & pageMask) + GetMemorySpacePageSize();
    } else {
        stackPtr = (uintptr_t*)CONTEXT_SP(context);
        stackTop = (CONTEXT_SP(context) & pageMask) + GetMemorySpacePageSize();
    }

    while (i && (uintptr_t)stackPtr < stackTop) {
        uintptr_t value = stackPtr[0];

        // Check for userspace code address
        if (value >= GetMachine()->MemoryMap.UserCode.Start &&
            value < (GetMachine()->MemoryMap.UserCode.Start + GetMachine()->MemoryMap.UserCode.Length) &&
            context != NULL) {
            DEBUG("%" PRIuIN " - 0x%" PRIxIN "", maxFrames - i, value);
            i--;
        }

        // Check for kernelspace code address
        if (value >= 0x100000 && value < 0x200000 && context == NULL) {
            DEBUG("%" PRIuIN " - 0x%" PRIxIN "", maxFrames - i, value);
            i--;
        }
        stackPtr++;
    }
    return OS_EOK;
}

oserr_t
DebugMemory(
    _In_Opt_ const char*    Description,
    _In_     void*          Address,
    _In_     size_t         Length)
{
    // Variables
    uint8_t Buffer[17];
    uint8_t *pc = (unsigned char*)Address;
    size_t i;

    // Output description if given.
    if (Description != NULL) {
        TRACE("%s:", Description);
    }

    if (Length == 0) {
        ERROR("ZERO LENGTH");
        return OS_EUNKNOWN;
    }

    // Process every byte in the data.
    for (i = 0; i < Length; i++) {
        // Multiple of 16 means new line (with line offset).

        if ((i % 16) == 0) {
            // Just don't print ASCII for the zeroth line.
            // TODO: build all this in a string buffer
            if (i != 0) {
                LogAppendMessage(OSSYSLOGLEVEL_TRACE, "  %s\n", Buffer);
            }

            // Output the offset.
            LogAppendMessage(OSSYSLOGLEVEL_TRACE, "  %04x ", i);
        }

        // Now the hex code for the specific character.
        LogAppendMessage(OSSYSLOGLEVEL_TRACE, " %02x", pc[i]);

        // And store a printable ASCII character for later.
        if ((pc[i] < 0x20) || (pc[i] > 0x7e)) {
            Buffer[i % 16] = '.';
        }
        else {
            Buffer[i % 16] = pc[i];
        }
        Buffer[(i % 16) + 1] = '\0';
    }

    // Pad out last line if not exactly 16 characters.
    while ((i % 16) != 0) {
        LogAppendMessage(OSSYSLOGLEVEL_TRACE, "   ");
        i++;
    }

    // And print the final ASCII bit.
    LogAppendMessage(OSSYSLOGLEVEL_TRACE, "  %s\n", Buffer);
    return OS_EOK;
}

/* Disassembles Memory */
//char *get_instructions_at_mem(uintptr_t address)
//{
//    /* We debug 50 bytes of memory */
//    int n;
//    int num_instructions = 1; /* Debug, normal 50 */
//    char *instructions = (char*)kmalloc(0x1000);
//    uintptr_t pointer = address;
//
//    memset(instructions, 0, 0x1000);
//    for (n = 0; n < num_instructions; n++)
//    {
//        INSTRUCTION inst;
//        char inst_str[64];
//
//        /* Get instruction */
//        get_instruction(&inst, (void*)pointer, MODE_32);
//
//        /* Translate */
//        get_instruction_string(&inst, FORMAT_ATT, 0, inst_str, sizeof(inst_str));
//
//        /* Append to list */
//        if (n == 0)
//        {
//            strcpy(instructions, inst_str);
//            strcat(instructions, "\n");
//        }
//        else
//        {
//            strcat(instructions, inst_str);
//            strcat(instructions, "\n");
//        }
//        pointer += inst.length;
//    }
//
//    return instructions;
//}


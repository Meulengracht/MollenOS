/**
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
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * MollenOS Debugging Interface
 * - Contains the shared kernel debugging interface and tools
 *   available for tracing and debugging
 */

#define __MODULE "DBGI"
//#define __TRACE

#include <arch/thread.h>
#include <arch/utils.h>
#include <debug.h>
#include <memoryspace.h>
#include <machine.h>
#include <stdio.h>

OsStatus_t
DebugSingleStep(
    _In_ Context_t* Context)
{
    TRACE("DebugSingleStep(IP 0x%" PRIxIN ")", CONTEXT_IP(Context));
    _CRT_UNUSED(Context);
    return OsSuccess;
}

OsStatus_t
DebugBreakpoint(
    _In_ Context_t* Context)
{
    TRACE("DebugBreakpoint(IP 0x%" PRIxIN ")", CONTEXT_IP(Context));
    _CRT_UNUSED(Context);
    return OsSuccess;
}

OsStatus_t
DebugPageFault(
    _In_ Context_t* context,
    _In_ uintptr_t  address)
{
    MemoryDescriptor_t descriptor;
    MemorySpace_t*     memorySpace = GetCurrentMemorySpace();
    uintptr_t          physicalAddress;
    OsStatus_t         osStatus;
    TRACE("DebugPageFault(context->ip=0x%" PRIxIN ", address=0x%" PRIxIN ")", CONTEXT_IP(context), address);

    // get information about the allocation
    osStatus = MemorySpaceQuery(memorySpace, address, &descriptor);
    if (osStatus == OsSuccess) {
        // userspace allocation, perform additional checks.
        // 1) Was a guard page hit?
        // 2) Should the exception be propegated (i.e. memory handlers)
        if (descriptor.Attributes & MAPPING_GUARDPAGE) {
            // detect stack overflow
        }
        if (descriptor.Attributes & MAPPING_TRAPPAGE) {
            TRACE("DebugPageFault trappage hit 0x%" PRIxIN, address);
            osStatus = OsError; // return error
            goto exit;
        }
    }

    // Otherwise, commit the page and continue without anyone noticing, and handle race-conditions
    // if two different threads have accessed a reserved page. OsExists will be returned.
    osStatus = MemorySpaceCommit(
            memorySpace,
            address,
            &physicalAddress,
            GetMemorySpacePageSize(),
            0,
            0
    );
    if (osStatus == OsExists) {
        osStatus = OsSuccess;
    }

exit:
    return osStatus;
}

static OsStatus_t
DebugHaltAllProcessorCores(
    _In_ UUId_t         ExcludeId,
    _In_ SystemCpu_t*   Processor)
{
    SystemCpuCore_t* Iter;
    
    Iter = Processor->Cores;
    while (Iter) {
        if (CpuCoreId(Iter) == ExcludeId) {
            Iter = CpuCoreNext(Iter);
            continue;
        }
        
        if (CpuCoreState(Iter) & CpuStateRunning) {
            TxuMessageSend(CpuCoreId(Iter), CpuFunctionHalt, NULL, NULL, 1);
        }
        Iter = CpuCoreNext(Iter);
    }
    return OsSuccess;
}

OsStatus_t
DebugPanic(
    _In_ int         FatalityScope,
    _In_ Context_t*  Context,
    _In_ const char* Message, ...)
{
    Thread_t* currentThread;
    char      messageBuffer[256];
    va_list   arguments;
    UUId_t    coreId;

    ERROR("DebugPanic(Scope %" PRIiIN ")", FatalityScope);

    // Disable all other cores in system if the fault is kernel scope
    coreId = ArchGetProcessorCoreId();
    if (FatalityScope == FATAL_SCOPE_KERNEL) {
        if (list_count(&GetMachine()->SystemDomains) != 0) {
            foreach(i, &GetMachine()->SystemDomains) {
                SystemDomain_t* Domain = (SystemDomain_t*)i->value;
                DebugHaltAllProcessorCores(coreId, &Domain->CoreGroup);
            }
        }
        else {
            DebugHaltAllProcessorCores(coreId, &GetMachine()->Processor);
        }
    }

    // Format the debug information
    va_start(arguments, Message);
    vsprintf(&messageBuffer[0], Message, arguments);
    va_end(arguments);
    LogSetRenderMode(1);
    LogAppendMessage(LOG_ERROR, &messageBuffer[0]);
    
    // Log cpu and threads
    currentThread = ThreadCurrentForCore(coreId);
    if (currentThread != NULL) {
        LogAppendMessage(LOG_ERROR, "Thread %s - %" PRIuIN " (Core %" PRIuIN ")!",
                         ThreadName(currentThread), ThreadHandle(currentThread), coreId);
    }
    
    if (Context) {
        ArchThreadContextDump(Context);
        DebugStackTrace(Context, 8);
    }

    // Handle based on the scope of the fatality
    if (FatalityScope == FATAL_SCOPE_KERNEL) {
        ArchProcessorHalt();
    }
    else if (FatalityScope == FATAL_SCOPE_PROCESS) {
        // @todo
    }
    else if (FatalityScope == FATAL_SCOPE_THREAD) {
        // @todo
    }
    else {
        ERROR("Encounted an unkown fatality scope %" PRIiIN "", FatalityScope);
        ArchProcessorHalt();
    }
    for(;;);
    return OsSuccess;
}

OsStatus_t
DebugStackTrace(
    _In_ Context_t* context,
    _In_ size_t     maxFrames)
{
    // Derive stack pointer from the argument
    uintptr_t* StackPtr;
    uintptr_t  StackLmt;
    uintptr_t  PageMask = ~(GetMemorySpacePageSize() - 1);
    size_t     Itr      = maxFrames;

    // Use local or given?
    if (context == NULL) {
        StackPtr = (uintptr_t*)&maxFrames;
        StackLmt = ((uintptr_t)StackPtr & PageMask) + GetMemorySpacePageSize();
    }
    else if (IS_USER_STACK(&GetMachine()->MemoryMap, CONTEXT_USERSP(context))) {
        StackPtr = (uintptr_t*)CONTEXT_USERSP(context);
        StackLmt = (CONTEXT_USERSP(context) & PageMask) + GetMemorySpacePageSize();
    }
    else {
        StackPtr = (uintptr_t*)CONTEXT_SP(context);
        StackLmt = (CONTEXT_SP(context) & PageMask) + GetMemorySpacePageSize();
    }

    while (Itr && (uintptr_t)StackPtr < StackLmt) {
        uintptr_t Value = StackPtr[0];

        // Check for userspace code address
        if (Value >= GetMachine()->MemoryMap.UserCode.Start && 
            Value < (GetMachine()->MemoryMap.UserCode.Start + GetMachine()->MemoryMap.UserCode.Length) &&
            context != NULL) {
            DEBUG("%" PRIuIN " - 0x%" PRIxIN "", maxFrames - Itr, Value);
            Itr--;
        }

        // Check for kernelspace code address
        if (Value >= 0x100000 && Value < 0x200000 && context == NULL) {
            DEBUG("%" PRIuIN " - 0x%" PRIxIN "", maxFrames - Itr, Value);
            Itr--;
        }
        StackPtr++;
    }
    return OsSuccess;
}

OsStatus_t
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
        return OsError;
    }

    // Process every byte in the data.
    for (i = 0; i < Length; i++) {
        // Multiple of 16 means new line (with line offset).

        if ((i % 16) == 0) {
            // Just don't print ASCII for the zeroth line.
            if (i != 0) {
                LogAppendMessage(LOG_RAW, "  %s\n", Buffer);
            }

            // Output the offset.
            LogAppendMessage(LOG_RAW, "  %04x ", i);
        }

        // Now the hex code for the specific character.
        LogAppendMessage(LOG_RAW, " %02x", pc[i]);

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
        LogAppendMessage(LOG_RAW, "   ");
        i++;
    }

    // And print the final ASCII bit.
    LogAppendMessage(LOG_RAW, "  %s\n", Buffer);
    return OsSuccess;
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


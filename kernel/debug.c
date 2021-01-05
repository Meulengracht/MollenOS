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
 * MollenOS Debugging Interface
 * - Contains the shared kernel debugging interface and tools
 *   available for tracing and debugging
 */
#define __MODULE        "DBGI"
//#define __TRACE

#include "../librt/libds/pe/pe.h"
#include <modules/manager.h>
#include <arch/utils.h>
#include <memoryspace.h>
#include <interrupts.h>
#include <deviceio.h>
#include <machine.h>
#include <handle.h>
#include <stdio.h>
#include <debug.h>
#include <heap.h>

static OsStatus_t
DebugPageMemorySpaceHandlers(
    _In_ Context_t* Context,
    _In_ uintptr_t  Address)
{
    MemorySpace_t * Space = GetCurrentMemorySpace();
    OsStatus_t           Status = OsError;

    if (Space->Context != NULL) {
        foreach(i, Space->Context->MemoryHandlers) {
            MemoryMappingHandler_t * Handler = (MemoryMappingHandler_t*)i->value;
            if (ISINRANGE(Address, Handler->Address, (Handler->Address + Handler->Length) - 1)) {
                ERROR("Implement support for MemorySpaceHandlers");
                for(;;);
                //SignalQueue(__FILEMANAGER_TARGET, SIGINT, Handler->Handle, (void*)Address);
                // @todo manually switch to next thread
                // ThreadingAdvance();
                // ContextLoad();
                break;
            }
        }
    }
    return Status;
}

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
    _In_ Context_t* Context,
    _In_ uintptr_t  Address)
{
    MemorySpace_t * Space = GetCurrentMemorySpace();
    uintptr_t            PhysicalAddress;
    OsStatus_t           Status;
    
    TRACE("DebugPageFault(IP 0x%" PRIxIN ", Address 0x%" PRIxIN ")", 
        CONTEXT_IP(Context), Address);

    if (Space->Context != NULL) {
        if (DebugPageMemorySpaceHandlers(Context, Address) == OsSuccess) {
            return OsSuccess;
        }
    }
    
    Status = MemorySpaceCommit(Space, Address, &PhysicalAddress, 
        GetMemorySpacePageSize(), 0);
    if (Status == OsExists) {
        Status = OsSuccess;
    }
    return Status;
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
    Thread_t *CurrentThread;
    char MessageBuffer[256];
    va_list Arguments;
    UUId_t CoreId;

    ERROR("DebugPanic(Scope %" PRIiIN ")", FatalityScope);

    // Disable all other cores in system if the fault is kernel scope
    CoreId = ArchGetProcessorCoreId();
    if (FatalityScope == FATAL_SCOPE_KERNEL) {
        if (list_count(&GetMachine()->SystemDomains) != 0) {
            foreach(i, &GetMachine()->SystemDomains) {
                SystemDomain_t* Domain = (SystemDomain_t*)i->value;
                DebugHaltAllProcessorCores(CoreId, &Domain->CoreGroup);
            }
        }
        else {
            DebugHaltAllProcessorCores(CoreId, &GetMachine()->Processor);
        }
    }

    // Format the debug information
    va_start(Arguments, Message);
    vsprintf(&MessageBuffer[0], Message, Arguments);
    va_end(Arguments);
    LogSetRenderMode(1);
    LogAppendMessage(LOG_ERROR, &MessageBuffer[0]);
    
    // Log cpu and threads
    CurrentThread = ThreadCurrentForCore(CoreId);
    if (CurrentThread != NULL) {
        LogAppendMessage(LOG_ERROR, "Thread %s - %" PRIuIN " (Core %" PRIuIN ")!",
            ThreadName(CurrentThread), ThreadHandle(CurrentThread), CoreId);
    }
    
    if (Context) {
        ArchDumpThreadContext(Context);
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

/* DebugGetModuleByAddress
 * Retrieves the module (Executable) at the given address */
OsStatus_t
DebugGetModuleByAddress(
    _In_  SystemModule_t* Module,
    _In_  uintptr_t       Address,
    _Out_ uintptr_t*      Base,
    _Out_ char**          Name)
{
    // Validate that the address is within userspace
    if (Address >= GetMachine()->MemoryMap.UserCode.Start && 
        Address < (GetMachine()->MemoryMap.UserCode.Start + GetMachine()->MemoryMap.UserCode.Length)) {
        // Sanitize whether or not a process was running
        if (Module != NULL && Module->Executable != NULL) {
            uintptr_t PmBase = Module->Executable->VirtualAddress;
            char *PmName     = (char*)MStringRaw(Module->Executable->Name);

            // Was it not main executable?
            if (Address > (Module->Executable->CodeBase + Module->Executable->CodeSize)) {
                // Iterate libraries to find the sinner
                if (Module->Executable->Libraries != NULL) {
                    foreach(i, Module->Executable->Libraries) {
                        PeExecutable_t* Lib = i->value;
                        if (Address >= Lib->CodeBase && Address < (Lib->CodeBase + Lib->CodeSize)) {
                            PmName = (char*)MStringRaw(Lib->Name);
                            PmBase = Lib->VirtualAddress;
                        }
                    }
                }
            }
            *Base = PmBase;
            *Name = PmName;
            return OsSuccess;
        }
    }
    *Base = 0;
    *Name = NULL;
    return OsError;
}

OsStatus_t
DebugStackTrace(
    _In_ Context_t* Context,
    _In_ size_t     MaxFrames)
{
    // Derive stack pointer from the argument
    uintptr_t* StackPtr;
    uintptr_t  StackLmt;
    uintptr_t  PageMask = ~(GetMemorySpacePageSize() - 1);
    size_t     Itr      = MaxFrames;

    // Use local or given?
    if (Context == NULL) {
        StackPtr = (uintptr_t*)&MaxFrames;
        StackLmt = ((uintptr_t)StackPtr & PageMask) + GetMemorySpacePageSize();
    }
    else if (CONTEXT_USERSP(Context) != 0) {
        StackPtr = (uintptr_t*)CONTEXT_USERSP(Context);
        StackLmt = (CONTEXT_USERSP(Context) & PageMask) + GetMemorySpacePageSize();
    }
    else {
        StackPtr = (uintptr_t*)CONTEXT_SP(Context);
        StackLmt = (CONTEXT_SP(Context) & PageMask) + GetMemorySpacePageSize();
    }

    while (Itr && (uintptr_t)StackPtr < StackLmt) {
        uintptr_t Value = StackPtr[0];
        uintptr_t Base  = 0;
        char *Name      = NULL;

        // Check for userspace code address
        if (Value >= GetMachine()->MemoryMap.UserCode.Start && 
            Value < (GetMachine()->MemoryMap.UserCode.Start + GetMachine()->MemoryMap.UserCode.Length) &&
            Context != NULL) {
            if (DebugGetModuleByAddress(GetCurrentModule(), Value, &Base, &Name) == OsSuccess) {
                WRITELINE("%" PRIuIN " - 0x%" PRIxIN " (%s)", MaxFrames - Itr, (Value - Base), Name);
                
            }
            else {
                WRITELINE("%" PRIuIN " - 0x%" PRIxIN "", MaxFrames - Itr, Value);
            }
            Itr--;
        }

        // Check for kernelspace code address
        if (Value >= 0x100000 && Value < 0x200000 && Context == NULL) {
            WRITELINE("%" PRIuIN " - 0x%" PRIxIN "", MaxFrames - Itr, Value);
            Itr--;
        }
        StackPtr++;
    }
    return OsSuccess;
}

/* DebugMemory 
 * Dumps memory in the form of <data> <string> at the
 * given address and length of memory dump */
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


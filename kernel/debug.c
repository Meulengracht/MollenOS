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
#include <system/utils.h>
#include <memoryspace.h>
#include <interrupts.h>
#include <deviceio.h>
#include <machine.h>
#include <handle.h>
#include <stdio.h>
#include <debug.h>
#include <heap.h>

/* Page-fault handlers for different page-fault areas. 
 * Static storage to only allow a maximum handlers. */
static struct MCorePageFaultHandler_t {
    uintptr_t   AreaStart;
    uintptr_t   AreaEnd;
    OsStatus_t (*AreaHandler)(Context_t *Context, uintptr_t Address);
} PageFaultHandlers[8] = { { 0 } };

/* DebugSingleStep
 * Handles the SingleStep trap on a higher level 
 * and the actual interrupt/exception should be propegated
 * to this event handler */
OsStatus_t
DebugSingleStep(
    _In_ Context_t* Context)
{
    TRACE("DebugSingleStep(IP 0x%x)", CONTEXT_IP(Context));
    _CRT_UNUSED(Context);
    return OsSuccess;
}

/* DebugBreakpoint
 * Handles the Breakpoint trap on a higher level 
 * and the actual interrupt/exception should be propegated
 * to this event handler */
OsStatus_t
DebugBreakpoint(
    _In_ Context_t* Context)
{
    TRACE("DebugBreakpoint(IP 0x%x)", CONTEXT_IP(Context));
    _CRT_UNUSED(Context);
    return OsSuccess;
}

/* DebugPageFault
 * Handles page-fault and either validates or invalidates
 * that the address is valid. In case of valid address it automatically
 * maps in the page and returns OsSuccess */
OsStatus_t
DebugPageFault(
    _In_ Context_t*     Context,
    _In_ uintptr_t      Address)
{
    TRACE("DebugPageFault(IP 0x%x, Address 0x%x)", CONTEXT_IP(Context), Address);
    for (int i = 0; i < 8; i++) {
        if (PageFaultHandlers[i].AreaHandler == NULL) {
            break;
        }
        if (ISINRANGE(Address, PageFaultHandlers[i].AreaStart, PageFaultHandlers[i].AreaEnd - 1)) {
            if (PageFaultHandlers[i].AreaHandler(Context, Address) == OsSuccess) {
                return OsSuccess;
            }
        }
    }
    return OsError;
}

/* DebugHaltAllProcessorCores
 * Halts all processor cores present in the processor. */
OsStatus_t
DebugHaltAllProcessorCores(
    _In_ UUId_t         ExcludeId,
    _In_ SystemCpu_t*   Processor)
{
    if (ExcludeId != Processor->PrimaryCore.Id) {
        InterruptProcessorCore(Processor->PrimaryCore.Id, CpuInterruptHalt);
    }

    for (int i = 0; i < Processor->NumberOfCores - 1; i++) {
        if (ExcludeId != Processor->ApplicationCores[i].Id) {
            InterruptProcessorCore(Processor->ApplicationCores[i].Id, CpuInterruptHalt);
        }
    }
    return OsSuccess;
}

/* DebugPanic
 * Kernel panic function - Call this to enter panic mode
 * and disrupt normal functioning. This function does not
 * return again */
OsStatus_t
DebugPanic(
    _In_ int         FatalityScope,
    _In_ Context_t*  Context,
    _In_ const char* Module,
    _In_ const char* Message, ...)
{
    MCoreThread_t *CurrentThread;
    char MessageBuffer[256];
    va_list Arguments;
    UUId_t CoreId;

    TRACE("DebugPanic(Scope %i)", FatalityScope);

    // Disable all other cores in system if the fault is kernel scope
    CoreId = CpuGetCurrentId();
    if (FatalityScope == FATAL_SCOPE_KERNEL) {
        if (CollectionLength(&GetMachine()->SystemDomains) != 0) {
            foreach(NumaNode, &GetMachine()->SystemDomains) {
                SystemDomain_t* Domain = (SystemDomain_t*)NumaNode;
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
    LogAppendMessage(LogError, Module, &MessageBuffer[0]);

    // Log cpu and threads
    CurrentThread = ThreadingGetCurrentThread(CoreId);
    if (CurrentThread != NULL) {
        LogAppendMessage(LogError, Module, "Thread %s - %u (Core %u)!",
            CurrentThread->Name, CurrentThread->Id, CoreId);
        if (CurrentThread->Flags & THREADING_IMPERSONATION) {
            // how should we do this
        }
    }
    ThreadingDebugPrint();
    DebugStackTrace(Context, 8);

    // Handle based on the scope of the fatality
    if (FatalityScope == FATAL_SCOPE_KERNEL) {
        CpuHalt();
    }
    else if (FatalityScope == FATAL_SCOPE_PROCESS) {
        // @todo
    }
    else if (FatalityScope == FATAL_SCOPE_THREAD) {
        // @todo
    }
    else {
        ERROR("Encounted an unkown fatality scope %i", FatalityScope);
        CpuHalt();
    }
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
                    foreach(lNode, Module->Executable->Libraries) {
                        PeExecutable_t* Lib = (PeExecutable_t*)lNode->Data;
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

/* DebugStackTrace
 * Performs a verbose stack trace in the current context 
 * Goes back a maximum of <MaxFrames> in the stack */
OsStatus_t
DebugStackTrace(
    _In_ Context_t* Context,
    _In_ size_t     MaxFrames)
{
    // Derive stack pointer from the argument
    uintptr_t* StackPtr;
    uintptr_t  StackLmt;
    uintptr_t  PageMask = ~(GetSystemMemoryPageSize() - 1);
    size_t     Itr      = MaxFrames;

    // Use local or given?
    if (Context == NULL) {
        StackPtr = (uintptr_t*)&MaxFrames;
        StackLmt = ((uintptr_t)StackPtr & PageMask) + GetSystemMemoryPageSize();
    }
    else if (CONTEXT_USERSP(Context) != 0) {
        StackPtr = (uintptr_t*)CONTEXT_USERSP(Context);
        StackLmt = (CONTEXT_USERSP(Context) & PageMask) + GetSystemMemoryPageSize();
    }
    else {
        StackPtr = (uintptr_t*)CONTEXT_SP(Context);
        StackLmt = (CONTEXT_SP(Context) & PageMask) + GetSystemMemoryPageSize();
    }

    while (Itr && (uintptr_t)StackPtr < StackLmt) {
        uintptr_t Value = StackPtr[0];
        uintptr_t Base  = 0;
        char *Name      = NULL;
        if (Value >= GetMachine()->MemoryMap.UserCode.Start && 
            Value < (GetMachine()->MemoryMap.UserCode.Start + GetMachine()->MemoryMap.UserCode.Length)) {
            if (DebugGetModuleByAddress(GetCurrentModule(), Value, &Base, &Name) == OsSuccess) {
                WRITELINE("%u - 0x%x (%s)", MaxFrames - Itr, (Value - Base), Name);
                
            }
            else {
                WRITELINE("%u - 0x%x", MaxFrames - Itr, Value);
            }
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
                LogAppendMessage(LogRaw, "EMPT", "  %s\n", Buffer);
            }

            // Output the offset.
            LogAppendMessage(LogRaw, "EMPT", "  %04x ", i);
        }

        // Now the hex code for the specific character.
        LogAppendMessage(LogRaw, "EMPT", " %02x", pc[i]);

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
        LogAppendMessage(LogRaw, "EMPT", "   ");
        i++;
    }

    // And print the final ASCII bit.
    LogAppendMessage(LogRaw, "EMPT", "  %s\n", Buffer);
    return OsSuccess;
}

/* DebugPageMemorySpaceHandlers
 * Checks for memory access that was memory handler related and valid */
OsStatus_t
DebugPageMemorySpaceHandlers(
    _In_ Context_t* Context,
    _In_ uintptr_t  Address)
{
    SystemMemorySpace_t* Space  = GetCurrentSystemMemorySpace();
    OsStatus_t           Status = OsError;

    foreach(Node, Space->MemoryHandlers) {
        SystemMemoryMappingHandler_t* Handler = (SystemMemoryMappingHandler_t*)Node;
        if (ISINRANGE(Address, Handler->Address, (Handler->Address + Handler->Length) - 1)) {
            __KernelInterruptDriver(__SESSIONMANAGER_TARGET, 0, (void*)Handler->Handle);
            Status = WaitForHandles(&Handler->Handle, 1, 1, 0);
            break;
        }
    }
    return Status;
}

/* DebugPageFaultProcessHeapMemory
 * Checks for memory access that was (process) heap related and valid */
OsStatus_t
DebugPageFaultProcessHeapMemory(
    _In_ Context_t* Context,
    _In_ uintptr_t  Address)
{
    SystemMemorySpace_t* Space     = GetCurrentSystemMemorySpace();
    Flags_t              PageFlags = MAPPING_USERSPACE | MAPPING_FIXED;

    if (Space->HeapSpace != NULL) {
        if (Space->MemoryHandlers != NULL && 
            DebugPageMemorySpaceHandlers(Context, Address) == OsSuccess) {
            return OsSuccess;
        }

        // If the mapping is a heap address we need to check for device-io mapping
        if (BlockBitmapValidateState(Space->HeapSpace, Address, 1) == OsSuccess) {
            uintptr_t ExistingPhysical = ValidateDeviceIoMemoryAddress(Address);
            if (ExistingPhysical != 0) {
                return CreateSystemMemorySpaceMapping(Space, &ExistingPhysical, &Address, 
                    GetSystemMemoryPageSize(), PageFlags | MAPPING_NOCACHE | MAPPING_PROVIDED, __MASK);
            }
            return CreateSystemMemorySpaceMapping(Space, NULL, &Address, GetSystemMemoryPageSize(), PageFlags, __MASK);
        }
    }
    return OsSuccess;
}

/* DebugPageFaultKernelHeapMemory
 * Checks for memory access that was (kernel) heap related and valid */
OsStatus_t
DebugPageFaultKernelHeapMemory(
    _In_ Context_t* Context,
    _In_ uintptr_t  Address)
{
    if (HeapValidateAddress(HeapGetKernel(), Address) == OsSuccess) {
        return CreateSystemMemorySpaceMapping(GetCurrentSystemMemorySpace(), NULL, &Address, 
            GetSystemMemoryPageSize(), MAPPING_FIXED, __MASK);
    }
    return OsError;
}

/* DebugPageFaultThreadMemory
 * Checks for memory access that was io-space related and valid */
OsStatus_t
DebugPageFaultThreadMemory(
    _In_ Context_t* Context,
    _In_ uintptr_t  Address)
{
    return CreateSystemMemorySpaceMapping(GetCurrentSystemMemorySpace(), NULL, &Address, 
                GetSystemMemoryPageSize(), MAPPING_USERSPACE | MAPPING_FIXED, __MASK);
}

/* DebugInstallPageFaultHandlers
 * Install page-fault handlers. Should be called as soon as memory setup phase is done */
OsStatus_t
DebugInstallPageFaultHandlers(
	_In_ SystemMemoryMap_t*	MemoryMap)
{
    TRACE("DebugInstallPageFaultHandlers()");

    // Heap memory handler
    PageFaultHandlers[0].AreaStart      = MemoryMap->SystemHeap.Start;
    PageFaultHandlers[0].AreaEnd        = MemoryMap->SystemHeap.Start + MemoryMap->SystemHeap.Length;
    PageFaultHandlers[0].AreaHandler    = DebugPageFaultKernelHeapMemory;

    // Process heap memory handler
    PageFaultHandlers[1].AreaStart      = MemoryMap->UserHeap.Start;
    PageFaultHandlers[1].AreaEnd        = MemoryMap->UserHeap.Start + MemoryMap->UserHeap.Length;
    PageFaultHandlers[1].AreaHandler    = DebugPageFaultProcessHeapMemory;

    // Thread-specific memory handler
    PageFaultHandlers[2].AreaStart      = MemoryMap->ThreadArea.Start;
    PageFaultHandlers[2].AreaEnd        = MemoryMap->ThreadArea.Start + MemoryMap->ThreadArea.Length;
    PageFaultHandlers[2].AreaHandler    = DebugPageFaultThreadMemory;
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
//    /* Null */
//    memset(instructions, 0, 0x1000);
//
//    /* Do it! */
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
//
//        /* Increament Pointer */
//        pointer += inst.length;
//    }
//
//    return instructions;
//}


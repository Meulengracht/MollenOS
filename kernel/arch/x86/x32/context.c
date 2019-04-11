/* MollenOS
 *
 * Copyright 2011, Philip Meulengracht
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
 * MollenOS X86-32 Thread Contexts
 */
#define __MODULE        "CTXT"
//#define __TRACE

#include <os/context.h>
#include <threading.h>
#include <assert.h>
#include <thread.h>
#include <string.h>
#include <memory.h>
#include <stdio.h>
#include <debug.h>
#include <heap.h>
#include <log.h>
#include <gdt.h>

void
ContextReset(
    _In_ Context_t* Context,
    _In_ int        ContextType,
	_In_ uintptr_t  EntryAddress,
    _In_ uintptr_t  ReturnAddress,
    _In_ uintptr_t  Argument0,
    _In_ uintptr_t  Argument1)
{
    uint32_t   DataSegment    = 0,
               ExtraSegment   = 0,
               CodeSegment    = 0, 
               StackSegment   = 0;
    uintptr_t  EbpInitial     = 0;
    
    if (ContextType == THREADING_CONTEXT_LEVEL0 || ContextType == THREADING_CONTEXT_SIGNAL0) {
        CodeSegment     = GDT_KCODE_SEGMENT;
        ExtraSegment    = StackSegment = DataSegment = GDT_KDATA_SEGMENT;
        EbpInitial      = ((uintptr_t)Context + sizeof(Context_t));
    }
    else if (ContextType == THREADING_CONTEXT_LEVEL1 || ContextType == THREADING_CONTEXT_SIGNAL1) {
        EbpInitial      = MEMORY_LOCATION_RING3_STACK_START;
        ExtraSegment    = GDT_EXTRA_SEGMENT + 0x03;

        // Now select the correct run-mode segments
        CodeSegment     = GDT_UCODE_SEGMENT + 0x03;
        StackSegment    = DataSegment = GDT_UDATA_SEGMENT + 0x03;

        // Set return address if its signal
        if (ContextType == THREADING_CONTEXT_SIGNAL1) {
            ReturnAddress = MEMORY_LOCATION_SIGNAL_RET;
        }
    }
    else {
        FATAL(FATAL_SCOPE_KERNEL, "ContextCreate::INVALID ContextType(%" PRIiIN ")", ContextType);
    }

    memset(Context, 0, sizeof(Context_t));

    // Setup segments for the stack
    Context->Ds = DataSegment;
    Context->Es = DataSegment;
    Context->Fs = DataSegment;
    Context->Gs = ExtraSegment;

    // Initialize registers to zero value
    Context->Ebp = EbpInitial;

    // Setup entry, eflags and the code segment
    Context->Eip    = EntryAddress;
    Context->Eflags = X86_THREAD_EFLAGS;
    Context->Cs     = CodeSegment;

    // Either initialize the ring3 stuff
    // or zero out the values
    Context->UserEsp = (uintptr_t)&Context->Arguments[0];
    Context->UserSs  = StackSegment;

    // Setup arguments
    Context->Arguments[0] = ReturnAddress;
    Context->Arguments[1] = Argument0;
    Context->Arguments[2] = Argument1;
}

Context_t*
ContextCreate(
    _In_ int ContextType)
{
    uintptr_t ContextAddress = 0;

    // Select proper segments based on context type and run-mode
    if (ContextType == THREADING_CONTEXT_LEVEL0 || ContextType == THREADING_CONTEXT_SIGNAL0) {
        ContextAddress  = ((uintptr_t)kmalloc(0x1000)) + 0x1000 - sizeof(Context_t);
    }
    else if (ContextType == THREADING_CONTEXT_LEVEL1 || ContextType == THREADING_CONTEXT_SIGNAL1) {
        if (ContextType == THREADING_CONTEXT_LEVEL1) {
            ContextAddress  = (MEMORY_LOCATION_RING3_STACK_START - sizeof(Context_t));
        }
        else {
            ContextAddress  = ((MEMORY_SEGMENT_SIGSTACK_BASE + MEMORY_SEGMENT_SIGSTACK_SIZE) - sizeof(Context_t));
        }
        CommitMemorySpaceMapping(GetCurrentMemorySpace(), NULL, ContextAddress, __MASK);
    }
    else {
        FATAL(FATAL_SCOPE_KERNEL, "ContextCreate::INVALID ContextType(%" PRIiIN ")", ContextType);
    }
    assert(ContextAddress != 0);
    return (Context_t*)ContextAddress;
}

void
ContextDestroy(
    _In_ Context_t* Context,
    _In_ int        ContextType)
{
    // If it is kernel space contexts they are allocated on the kernel heap,
    // otherwise they are cleaned up by the memory space
    if (ContextType == THREADING_CONTEXT_LEVEL0 || ContextType == THREADING_CONTEXT_SIGNAL0) {
        kfree(Context);
    }
}

OsStatus_t
ArchDumpThreadContext(
    _In_ Context_t *Context)
{
    // Dump general registers
    WRITELINE("EAX: 0x%" PRIxIN ", EBX 0x%" PRIxIN ", ECX 0x%" PRIxIN ", EDX 0x%" PRIxIN "",
        Context->Eax, Context->Ebx, Context->Ecx, Context->Edx);

    // Dump stack registers
    WRITELINE("ESP 0x%" PRIxIN " (UserESP 0x%" PRIxIN "), EBP 0x%" PRIxIN ", Flags 0x%" PRIxIN "",
        Context->Esp, Context->UserEsp, Context->Ebp, Context->Eflags);
        
    // Dump copy registers
    WRITELINE("ESI 0x%" PRIxIN ", EDI 0x%" PRIxIN "", Context->Esi, Context->Edi);

    // Dump segments
    WRITELINE("CS 0x%" PRIxIN ", DS 0x%" PRIxIN ", GS 0x%" PRIxIN ", ES 0x%" PRIxIN ", FS 0x%" PRIxIN "",
        Context->Cs, Context->Ds, Context->Gs, Context->Es, Context->Fs);

    // Dump IRQ information
    WRITELINE("IRQ 0x%" PRIxIN ", ErrorCode 0x%" PRIxIN ", UserSS 0x%" PRIxIN "",
        Context->Irq, Context->ErrorCode, Context->UserSs);
    return OsSuccess;
}

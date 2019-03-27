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
 * MollenOS X86-64 Thread Contexts
 */
#define __MODULE		"CTXT"
//#define __TRACE

#include <os/context.h>
#include <threading.h>
#include <thread.h>
#include <memory.h>
#include <debug.h>
#include <heap.h>
#include <log.h>
#include <gdt.h>
#include <string.h>
#include <stdio.h>

Context_t*
ContextCreate(
    _In_ Flags_t    ThreadFlags,
    _In_ int        ContextType,
	_In_ uintptr_t  EntryAddress,
    _In_ uintptr_t  ReturnAddress,
    _In_ uintptr_t  Argument0,
    _In_ uintptr_t  Argument1)
{
	Context_t *Context       = NULL;
    uint64_t DataSegment     = 0,
             ExtraSegment    = 0,
             CodeSegment     = 0, 
             StackSegment    = 0;
    uintptr_t ContextAddress = 0, 
              RbpInitial     = 0;

	TRACE("ContextCreate(ThreadFlags 0x%llx, Type %" PRIiIN ", Rip 0x%llx, Args 0x%llx)",
		ThreadFlags, ContextType, EntryAddress);

	// Select proper segments based on context type and run-mode
	if (ContextType == THREADING_CONTEXT_LEVEL0 || ContextType == THREADING_CONTEXT_SIGNAL0) {
		ContextAddress  = ((uintptr_t)kmalloc(PAGE_SIZE)) + PAGE_SIZE - sizeof(Context_t);
		CodeSegment     = GDT_KCODE_SEGMENT;
		ExtraSegment    = StackSegment = DataSegment = GDT_KDATA_SEGMENT;
		RbpInitial      = (ContextAddress + sizeof(Context_t));
	}
    else if (ContextType == THREADING_CONTEXT_LEVEL1 || ContextType == THREADING_CONTEXT_SIGNAL1) {
        if (ContextType == THREADING_CONTEXT_LEVEL1) {
		    ContextAddress  = (MEMORY_LOCATION_RING3_STACK_START - sizeof(Context_t));
        }
        else {
		    ContextAddress  = ((MEMORY_SEGMENT_SIGSTACK_BASE + MEMORY_SEGMENT_SIGSTACK_SIZE) - sizeof(Context_t));
        }
 		RbpInitial   = MEMORY_LOCATION_RING3_STACK_START;
        ExtraSegment = GDT_EXTRA_SEGMENT + 0x03;

        // Now select the correct run-mode segments
        if (THREADING_RUNMODE(ThreadFlags) == THREADING_USERMODE) {
            CodeSegment     = GDT_UCODE_SEGMENT + 0x03;
		    StackSegment    = DataSegment = GDT_UDATA_SEGMENT + 0x03;
        }
        else {
            FATAL(FATAL_SCOPE_KERNEL, "ContextCreate::INVALID THREADFLAGS(%" PRIuIN ")", ThreadFlags);
        }
        CommitMemorySpaceMapping(GetCurrentMemorySpace(), NULL, ContextAddress, __MASK);
    }
	else {
		FATAL(FATAL_SCOPE_KERNEL, "ContextCreate::INVALID ContextType(%" PRIiIN ")", ContextType);
	}

	// Initialize the context pointer
	Context = (Context_t*)ContextAddress;
    memset(Context, 0, sizeof(Context_t));

	// Setup segments for the stack
	Context->Ds  = DataSegment;
	Context->Es  = DataSegment;
	Context->Fs  = DataSegment;
	Context->Gs  = ExtraSegment;
	Context->Rbp = RbpInitial;

	// Setup entry, eflags and the code segment
	Context->Rip    = EntryAddress;
	Context->Rflags = X86_THREAD_EFLAGS;
	Context->Cs     = CodeSegment;

	// Either initialize the ring3 stuff or zero out the values
    Context->UserRsp = (uintptr_t)&Context->ReturnAddress;
    Context->UserSs  = StackSegment;

    // Setup arguments and return
    Context->ReturnAddress = ReturnAddress;
    Context->Rcx           = Argument0;
    Context->Rdx           = Argument1;
	return Context;
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
	WRITELINE("RAX: 0x%llx, RBX 0x%llx, RCX 0x%llx, RDX 0x%llx",
		Context->Rax, Context->Rbx, Context->Rcx, Context->Rdx);
	WRITELINE("R8: 0x%llx, R9 0x%llx, R10 0x%llx, R11 0x%llx",
		Context->R8, Context->R9, Context->R10, Context->R11);
	WRITELINE("R12: 0x%llx, R13 0x%llx, R14 0x%llx, R15 0x%llx",
		Context->R12, Context->R13, Context->R14, Context->R15);

	// Dump stack registers
	WRITELINE("RSP 0x%llx (UserRSP 0x%llx), RBP 0x%llx, Flags 0x%llx",
        Context->Rsp, Context->UserRsp, Context->Rbp, Context->Rflags);
        
    // Dump copy registers
	WRITELINE("RSI 0x%llx, RDI 0x%llx", Context->Rsi, Context->Rdi);

	// Dump segments
	WRITELINE("CS 0x%llx, DS 0x%llx, GS 0x%llx, ES 0x%llx, FS 0x%llx",
		Context->Cs, Context->Ds, Context->Gs, Context->Es, Context->Fs);

	// Dump IRQ information
	WRITELINE("IRQ 0x%llx, ErrorCode 0x%llx, UserSS 0x%llx",
		Context->Irq, Context->ErrorCode, Context->UserSs);
	return OsSuccess;
}

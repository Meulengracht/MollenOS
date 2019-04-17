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
#include <assert.h>
#include <debug.h>
#include <heap.h>
#include <log.h>
#include <gdt.h>
#include <string.h>
#include <stdio.h>

static void
ContextPush(
	_In_ uintptr_t** Address,
	_In_ uintptr_t   Value)
{
	(*Address)--;
	*(*Address) = Value;
}

void
ContextPushInterceptor(
    _In_     Context_t* Context,
    _In_     uintptr_t  Address,
    _In_Opt_ uintptr_t* SafeStack,
    _In_     uintptr_t  Argument0,
    _In_     uintptr_t  Argument1)
{
	// Ok so the tricky thing here is the interceptor must be pushed
	// on the USER stack, not the current context pointer which makes things
	// both easier and a bit more complex. We do not guarentee stack alignment
	// when creating interceptor functions.
	
	// @todo perform stack-safe operations, check if stack is ok before doing 
	// any of these pushes 
	
	// Push in reverse fashion, and have everything on stack to be able to restore
	// the default register states
	ContextPush((uintptr_t**)&Context->UserRsp, Context->Rip);
	ContextPush((uintptr_t**)&Context->UserRsp, Context->Rcx);
	ContextPush((uintptr_t**)&Context->UserRsp, Context->Rdx);
	ContextPush((uintptr_t**)&Context->UserRsp, Context->R8);
	
	// Set arguments
	Context->Rip = Address;
	Context->Rcx = Argument0;
	Context->Rdx = Argument1;
	Context->R8  = (uint64_t)SafeStack;
}

void
ContextReset(
    _In_ Context_t* Context,
    _In_ int        ContextType,
	_In_ uintptr_t  EntryAddress,
    _In_ uintptr_t  ReturnAddress,
    _In_ uintptr_t  Argument0,
    _In_ uintptr_t  Argument1)
{
    uint64_t  DataSegment  = 0,
              ExtraSegment = 0,
              CodeSegment  = 0, 
              StackSegment = 0;
    uintptr_t RbpInitial   = 0;
	
	// Select proper segments based on context type and run-mode
	if (ContextType == THREADING_CONTEXT_LEVEL0) {
		CodeSegment     = GDT_KCODE_SEGMENT;
		ExtraSegment    = StackSegment = DataSegment = GDT_KDATA_SEGMENT;
		RbpInitial      = ((uintptr_t)Context + sizeof(Context_t));
	}
    else if (ContextType == THREADING_CONTEXT_LEVEL1 || ContextType == THREADING_CONTEXT_SIGNAL) {
 		RbpInitial   = MEMORY_LOCATION_RING3_STACK_START;
        ExtraSegment = GDT_EXTRA_SEGMENT + 0x03;
        CodeSegment  = GDT_UCODE_SEGMENT + 0x03;
	    StackSegment = DataSegment = GDT_UDATA_SEGMENT + 0x03;
	    
        // Set return address if its signal, just in case
        if (ContextType == THREADING_CONTEXT_SIGNAL) {
        	EntryAddress  = MEMORY_LOCATION_SIGNAL_RET;
            ReturnAddress = MEMORY_LOCATION_SIGNAL_RET;
        }
    }
	else {
		FATAL(FATAL_SCOPE_KERNEL, "ContextCreate::INVALID ContextType(%" PRIiIN ")", ContextType);
	}

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
}

Context_t*
ContextCreate(
    _In_ int ContextType)
{
    uintptr_t ContextAddress = 0;

	// Select proper segments based on context type and run-mode
	if (ContextType == THREADING_CONTEXT_LEVEL0) {
		ContextAddress  = ((uintptr_t)kmalloc(PAGE_SIZE)) + PAGE_SIZE - sizeof(Context_t);
	}
    else if (ContextType == THREADING_CONTEXT_LEVEL1 || ContextType == THREADING_CONTEXT_SIGNAL) {
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
    if (ContextType == THREADING_CONTEXT_LEVEL0) {
        kfree(Context);
    }
}

OsStatus_t
ArchDumpThreadContext(
	_In_ Context_t* Context)
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

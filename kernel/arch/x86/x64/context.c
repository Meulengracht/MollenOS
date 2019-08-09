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
 * X86-64 Thread Contexts
 */
#define __MODULE "CTXT"
//#define __TRACE

#include <arch.h>
#include <assert.h>
#include <cpu.h>
#include <debug.h>
#include <gdt.h>
#include <heap.h>
#include <log.h>
#include <os/context.h>
#include <memory.h>
#include <memoryspace.h>
#include <string.h>
#include <stdio.h>
#include <threading.h>

//Identifier to detect newly reset stacks
#define CONTEXT_RESET_IDENTIFIER 0xB00B1E5

///////////////////////////
// LEVEL0 CONTEXT
// Because signals are not supported for kernel threads/contexts we do not
// need to support the PushInterceptor functionality, and thus we do not need
// nor can we actually provide seperation
//      top (stack)
// |  shadow-space   |
// |-----------------|
// |  return-address |
// |-----------------|
// |    context_t    |
// |                 |
// |-----------------|
//
// LEVEL1 && SIGNAL CONTEXT
// Contexts (runtime stacks) are created and located two different places
// within a single page. The layout looks like this. This layout is only
// valid on stack creation, and no longer after first used
//      top (stack)
// |  shadow-space   |
// |-----------------|
// |  return-address |--|--- points to fixed address to detect end of signal handling
// |-----------------|  |
// |      rcx        |  |
// |      rdx        |  |
// |      r8         |  |
// |-----------------|  |
// |    empty-space  |  |
// |                 |  |
// |                 |  |
// |                 |  |
// |                 |  | User-esp points to the base of the stack
// |-----------------|  |
// |    context_t    |  |
// |                 |--|
// |-----------------|  
// By doing this we can support adding interceptors when creating a new
// context, for handling signals this is effective.

static void
ContextPush(
	_In_ uintptr_t** Address,
	_In_ uintptr_t   Value)
{
	uintptr_t* Stack = *Address;
	*(--Stack) = Value;
	*Address = Stack;
}

void
ContextPushInterceptor(
    _In_ Context_t* Context,
    _In_ uintptr_t  Address,
    _In_ uintptr_t  Argument0,
    _In_ uintptr_t  Argument1,
    _In_ uintptr_t  Argument2)
{
	// ASSUMPTIONS
	// STACK MUST BE LEVEL1/SIGNAL
	// STACK MUST BE RESET BEFORE FIRST CALL TO INTERCEPTOR
	assert(Context->Rax == CONTEXT_RESET_IDENTIFIER);
	
	// Push in reverse fashion, and have everything on stack to be able to restore
	// the default register states. We cannot guarantee alignment on interceptor functions
	// as there is no way to restore the stack
	ContextPush((uintptr_t**)&Context->UserRsp, Context->Rip);
	ContextPush((uintptr_t**)&Context->UserRsp, Context->Rcx);
	ContextPush((uintptr_t**)&Context->UserRsp, Context->Rdx);
	ContextPush((uintptr_t**)&Context->UserRsp, Context->R8);
	
	// Set arguments
	Context->Rip = Address;
	Context->Rcx = Argument0;
	Context->Rdx = Argument1;
	Context->R8  = Argument2;
}

void
ContextReset(
    _In_ Context_t* Context,
    _In_ int        ContextType,
    _In_ uintptr_t  Address,
    _In_ uintptr_t  Argument0,
    _In_ uintptr_t  Argument1,
    _In_ uintptr_t  Argument2)
{
    uint64_t DataSegment   = 0;
    uint64_t ExtraSegment  = 0;
    uint64_t CodeSegment   = 0;
    uint64_t StackSegment  = 0;
    uint64_t RbpInitial    = 0;
    uint64_t RspReturn     = 0;
	
	// Reset context
    memset(Context, 0, sizeof(Context_t));

	// Select proper segments based on context type and run-mode
	if (ContextType == THREADING_CONTEXT_LEVEL0) {
		CodeSegment  = GDT_KCODE_SEGMENT;
		DataSegment  = GDT_KDATA_SEGMENT;
		ExtraSegment = GDT_KDATA_SEGMENT;
		StackSegment = GDT_KDATA_SEGMENT;
		RbpInitial   = ((uint64_t)Context + sizeof(Context_t));
		RspReturn    = (uint64_t)&Context->ReturnAddress;
	}
    else if (ContextType == THREADING_CONTEXT_LEVEL1 || ContextType == THREADING_CONTEXT_SIGNAL) {
    	uintptr_t* StackTopPointer;
        ExtraSegment = GDT_EXTRA_SEGMENT + 0x03;
        CodeSegment  = GDT_UCODE_SEGMENT + 0x03;
	    StackSegment = GDT_UDATA_SEGMENT + 0x03;
	    DataSegment  = GDT_UDATA_SEGMENT + 0x03;
	    
	    // Base should point to top of stack
	    if (ContextType == THREADING_CONTEXT_LEVEL1) {
 			RbpInitial = MEMORY_LOCATION_RING3_STACK_START;
	    }
	    else {
	    	RbpInitial = MEMORY_SEGMENT_SIGSTACK_BASE + MEMORY_SEGMENT_SIGSTACK_SIZE;
	    }
	    
	    // Initialize top part of stack, skip shadow space
	    StackTopPointer      = (uintptr_t*)(RbpInitial - (4 * 8));
	    *(--StackTopPointer) = MEMORY_LOCATION_SIGNAL_RET;
	    if (ContextType == THREADING_CONTEXT_SIGNAL) {
		    *(--StackTopPointer) = 0;
		    *(--StackTopPointer) = 0;
		    *(--StackTopPointer) = 0;
	    }
	    
		// Either initialize the ring3 stuff or zero out the values
	    RspReturn    = (uint64_t)StackTopPointer;
	    Context->Rax = CONTEXT_RESET_IDENTIFIER;
    }
	else {
		FATAL(FATAL_SCOPE_KERNEL, "ContextCreate::INVALID ContextType(%" PRIiIN ")", ContextType);
	}

	// Setup segments for the stack
	Context->Ds  = DataSegment;
	Context->Es  = DataSegment;
	Context->Fs  = DataSegment;
	Context->Gs  = ExtraSegment;
	Context->Rbp = RbpInitial;

	// Setup entry, eflags and the code segment
	Context->Rip     = Address;
	Context->Rflags  = CPU_EFLAGS_DEFAULT;
	Context->Cs      = CodeSegment;
	Context->UserRsp = RspReturn;
	Context->UserSs  = StackSegment;

    // Setup arguments and return
    Context->Rcx = Argument0;
    Context->Rdx = Argument1;
    Context->R8  = Argument2;
}

Context_t*
ContextCreate(
    _In_ int ContextType)
{
    uintptr_t ContextAddress = 0;

	// Select proper segments based on context type and run-mode
	if (ContextType == THREADING_CONTEXT_LEVEL0) {
		// Return a pointer to (STACK_TOP - SIZEOF(CONTEXT))
		ContextAddress = ((uintptr_t)kmalloc(PAGE_SIZE)) + PAGE_SIZE - sizeof(Context_t);
	}
    else if (ContextType == THREADING_CONTEXT_LEVEL1 || ContextType == THREADING_CONTEXT_SIGNAL) {
    	// For both level1 and signal, we return a pointer to STACK_TOP_PAGE_BOTTOM
        if (ContextType == THREADING_CONTEXT_LEVEL1) {
		    ContextAddress = MEMORY_LOCATION_RING3_STACK_START - PAGE_SIZE;
        }
        else {
		    ContextAddress = MEMORY_SEGMENT_SIGSTACK_BASE + MEMORY_SEGMENT_SIGSTACK_SIZE - PAGE_SIZE;
        }
        CommitMemorySpaceMapping(GetCurrentMemorySpace(), ContextAddress, NULL,
        	GetMemorySpacePageSize(), MAPPING_PHYSICAL_DEFAULT, __MASK);
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

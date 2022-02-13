/**
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
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * X86-64 Thread Contexts
 */

#define __MODULE "context"
//#define __TRACE

#include <assert.h>
#include <arch/x86/cpu.h>
#include <arch/x86/memory.h>
#include <arch/x86/x64/gdt.h>
#include <debug.h>
#include <log.h>
#include <memoryspace.h>
#include <string.h>
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
PushRegister(
	_In_ uintptr_t* StackReference,
	_In_ uintptr_t  Value)
{
	*StackReference -= sizeof(uint64_t);
	*((uintptr_t*)(*StackReference)) = Value;
}

static void
PushContextOntoStack(
	_In_ uintptr_t* StackReference,
    _In_ Context_t* Context)
{
	// Create space on the stack, then copy values onto the bottom of the
	// space subtracted.
	*StackReference -= sizeof(Context_t);
	memcpy((void*)(*StackReference), Context, sizeof(Context_t));
}

void
ArchThreadContextPushInterceptor(
    _In_ Context_t* Context,
    _In_ uintptr_t  TemporaryStack,
    _In_ uintptr_t  Address,
    _In_ uintptr_t  Argument0,
    _In_ uintptr_t  Argument1,
    _In_ uintptr_t  Argument2)
{
	uintptr_t NewStackPointer;
	
	TRACE("[context] [push_interceptor] stack 0x%" PRIxIN ", address 0x%" PRIxIN ", rip 0x%" PRIxIN,
		TemporaryStack, Address, Context->Rip);
	
	// On the previous stack, we would like to keep the Rip as it will be activated
	// before jumping to the previous address
	if (!TemporaryStack) {
		PushRegister(&Context->UserRsp, Context->Rip);
		
		NewStackPointer = Context->UserRsp;
		PushContextOntoStack(&NewStackPointer, Context);
	}
	else {
		NewStackPointer = TemporaryStack;
		
		PushRegister(&Context->UserRsp, Context->Rip);
		PushContextOntoStack(&NewStackPointer, Context);
	}

	// Store all information provided, and 
	Context->Rip = Address;
	Context->Rcx = NewStackPointer;
	Context->Rdx = Argument0;
	Context->R8  = Argument1;
	Context->R9  = Argument2;
	
	// Replace current stack with the one provided that has been adjusted for
	// the copy of the context structure
	Context->UserRsp = NewStackPointer;
}

void
ArchThreadContextReset(
    _In_ Context_t* context,
    _In_ int        contextType,
    _In_ uintptr_t  address,
    _In_ uintptr_t  argument)
{
    uint64_t codeSegment  = 0;
    uint64_t stackSegment = 0;
    uint64_t rbpInitial   = ((uint64_t)context + sizeof(Context_t));
    TRACE("[ArchThreadContextReset] %i: 0x%llx", contextType, context);
	
	// Reset context
    memset(context, 0, sizeof(Context_t));

	// Select proper segments based on context type and run-mode
	if (contextType == THREADING_CONTEXT_LEVEL0) {
        codeSegment  = GDT_KCODE_SEGMENT;
        stackSegment = GDT_KDATA_SEGMENT;
	}
	else if (contextType == THREADING_CONTEXT_LEVEL1 || contextType == THREADING_CONTEXT_SIGNAL) {
        codeSegment  = GDT_UCODE_SEGMENT + 0x03;
        stackSegment = GDT_UDATA_SEGMENT + 0x03;

		// Either initialize the ring3 stuff or zero out the values
	    context->Rax = CONTEXT_RESET_IDENTIFIER;
    }
	else {
		FATAL(FATAL_SCOPE_KERNEL, "ArchThreadContextCreate::INVALID ContextType(%" PRIiIN ")", contextType);
	}

	// Setup segments for the stack
    context->Rbp = rbpInitial;

	// Setup entry, eflags and the code segment
	context->Rip     = address;
    context->Rflags  = CPU_EFLAGS_DEFAULT;
    context->Cs      = codeSegment;
    context->UserRsp = (uint64_t)&context->ReturnAddress;
    context->UserSs  = stackSegment;

    // Setup arguments
    context->Rcx = argument;
}

Context_t*
ArchThreadContextCreate(
    _In_ int    contextType,
    _In_ size_t contextSize)
{
	OsStatus_t     status;
	uintptr_t      physicalContextAddress;
    uintptr_t      contextAddress = 0;
    unsigned int   placementFlags = 0;
    unsigned int   memoryFlags    = MAPPING_DOMAIN | MAPPING_GUARDPAGE;
	MemorySpace_t* memorySpace    = GetCurrentMemorySpace();

	if (contextType == THREADING_CONTEXT_LEVEL0) {
	    placementFlags = MAPPING_VIRTUAL_GLOBAL;
	}
	else if (contextType == THREADING_CONTEXT_LEVEL1 || contextType == THREADING_CONTEXT_SIGNAL) {
	    // this works because we are called from the thread that needs it
	    placementFlags = MAPPING_VIRTUAL_THREAD;
        memoryFlags |= MAPPING_USERSPACE;
	}
	else {
        FATAL(FATAL_SCOPE_KERNEL, "ArchThreadContextCreate::INVALID ContextType(%" PRIiIN ")", contextType);
    }

    // Return a pointer to (STACK_TOP - SIZEOF(CONTEXT))
    status = MemorySpaceMapReserved(memorySpace, &contextAddress, contextSize, memoryFlags, placementFlags);
    if (status != OsSuccess) {
        return NULL;
    }

    // Adjust pointer to top of stack and then commit the first stack page
    status = MemorySpaceCommit(
            memorySpace,
            contextAddress + (contextSize - sizeof(Context_t)),
            &physicalContextAddress,
            PAGE_SIZE,
            0,
            0
    );
    if (status != OsSuccess) {
        MemorySpaceUnmap(memorySpace, contextAddress, contextSize);
        return NULL;
    }

    contextAddress += contextSize - sizeof(Context_t);
    TRACE("[ArchThreadContextCreate] %i: 0x%llx", contextType, contextAddress);

	return (Context_t*)contextAddress;
}

void
ArchThreadContextDestroy(
    _In_ Context_t* context,
    _In_ int        contextType,
    _In_ size_t     contextSize)
{
    uintptr_t contextAddress;

    // do not touch LEVEL1+SIGNAL as they are mapped as thread memory in another space
    if (!context || contextType != THREADING_CONTEXT_LEVEL0) {
        return;
    }

    TRACE("[ArchThreadContextDestroy] 0x%llx", context);

    // adjust for size of context_t and then adjust back to base address
    contextAddress  = (uintptr_t)context;
    contextAddress += sizeof(Context_t);
    contextAddress -= contextSize;

    MemorySpaceUnmap(GetCurrentMemorySpace(), contextAddress, contextSize);
}

OsStatus_t
ArchThreadContextDump(
	_In_ Context_t* context)
{
	// Dump general registers
	DEBUG("RAX: 0x%llx, RBX 0x%llx, RCX 0x%llx, RDX 0x%llx",
          context->Rax, context->Rbx, context->Rcx, context->Rdx);
	DEBUG("R8: 0x%llx, R9 0x%llx, R10 0x%llx, R11 0x%llx",
          context->R8, context->R9, context->R10, context->R11);
	DEBUG("R12: 0x%llx, R13 0x%llx, R14 0x%llx, R15 0x%llx",
          context->R12, context->R13, context->R14, context->R15);

	// Dump stack registers
	DEBUG("RSP 0x%llx (UserRSP 0x%llx), RBP 0x%llx, Flags 0x%llx",
          context->Rsp, context->UserRsp, context->Rbp, context->Rflags);
        
    // Dump copy registers
	DEBUG("RIP 0x%llx", context->Rip);
	DEBUG("RSI 0x%llx, RDI 0x%llx", context->Rsi, context->Rdi);

	// Dump IRQ information
	DEBUG("IRQ 0x%llx, ErrorCode 0x%llx, UserSS 0x%llx",
          context->Irq, context->ErrorCode, context->UserSs);
	return OsSuccess;
}

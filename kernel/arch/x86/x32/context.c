/**
 * MollenOS
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
 * X86-32 Thread Contexts
 */

#define __MODULE "context"
//#define __TRACE

#include <arch.h>
#include <assert.h>
#include <cpu.h>
#include <debug.h>
#include <gdt.h>
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
// |-----------------|
// |  return-address |--|--- points to fixed address to detect end of signal handling
// |-----------------|  |
// |      eax        |  |
// |      ebx        |  |
// |      ecx        |  |
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
	*StackReference -= sizeof(uint32_t);
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
ContextPushInterceptor(
    _In_ Context_t* Context,
    _In_ uintptr_t  TemporaryStack,
    _In_ uintptr_t  Address,
    _In_ uintptr_t  Argument0,
    _In_ uintptr_t  Argument1,
    _In_ uintptr_t  Argument2)
{
	uintptr_t NewStackPointer;
	
	TRACE("[context] [push_interceptor] stack 0x%" PRIxIN ", address 0x%" PRIxIN ", rip 0x%" PRIxIN,
		TemporaryStack, Address, Context->Eip);
	
	// On the previous stack, we would like to keep the Rip as it will be activated
	// before jumping to the previous address
	if (!TemporaryStack) {
		PushRegister(&Context->UserEsp, Context->Eip);
		
		NewStackPointer = Context->UserEsp;
		PushContextOntoStack(&NewStackPointer, Context);
	}
	else {
		NewStackPointer = TemporaryStack;
		
		PushRegister(&Context->UserEsp, Context->Eip);
		PushContextOntoStack(&NewStackPointer, Context);
	}

	// Store all information provided, and 
	Context->Eip = Address;
	Context->Eax = NewStackPointer;
	Context->Ebx = Argument0;
	Context->Ecx = Argument1;
	Context->Edx = Argument2;
	
	// Replace current stack with the one provided that has been adjusted for
	// the copy of the context structure
	Context->UserEsp = NewStackPointer;
}

void
ContextReset(
    _In_ Context_t* Context,
    _In_ int        ContextType,
    _In_ uintptr_t  Address,
    _In_ uintptr_t  Argument)
{
    uint32_t DataSegment   = 0;
    uint32_t ExtraSegment  = 0;
    uint32_t CodeSegment   = 0;
    uint32_t StackSegment  = 0;
    uint32_t EbpInitial    = 0;
    
	// Reset context
    memset(Context, 0, sizeof(Context_t));

    if (ContextType == THREADING_CONTEXT_LEVEL0) {
		CodeSegment  = GDT_KCODE_SEGMENT;
		DataSegment  = GDT_KDATA_SEGMENT;
		ExtraSegment = GDT_KDATA_SEGMENT;
		StackSegment = GDT_KDATA_SEGMENT;
		EbpInitial   = ((uint32_t)Context + sizeof(Context_t));
    }
    else if (ContextType == THREADING_CONTEXT_LEVEL1 || ContextType == THREADING_CONTEXT_SIGNAL) {
        ExtraSegment = GDT_EXTRA_SEGMENT + 0x03;
        CodeSegment  = GDT_UCODE_SEGMENT + 0x03;
	    StackSegment = GDT_UDATA_SEGMENT + 0x03;
	    DataSegment  = GDT_UDATA_SEGMENT + 0x03;
        
	    // Base should point to top of stack
	    if (ContextType == THREADING_CONTEXT_LEVEL1) {
 			EbpInitial = MEMORY_LOCATION_RING3_STACK_START;
	    }
	    else {
	    	EbpInitial = MEMORY_SEGMENT_SIGSTACK_BASE + MEMORY_SEGMENT_SIGSTACK_SIZE;
	    }
        
		// Either initialize the ring3 stuff or zero out the values
	    Context->Eax = CONTEXT_RESET_IDENTIFIER;
    }
    else {
        FATAL(FATAL_SCOPE_KERNEL, "ContextCreate::INVALID ContextType(%" PRIiIN ")", ContextType);
    }

    // Setup segments for the stack
    Context->Ds  = DataSegment;
    Context->Es  = DataSegment;
    Context->Fs  = DataSegment;
    Context->Gs  = ExtraSegment;
    Context->Ebp = EbpInitial;

    // Setup entry, eflags and the code segment
    Context->Eip     = Address;
    Context->Eflags  = CPU_EFLAGS_DEFAULT;
    Context->Cs      = CodeSegment;
    Context->UserEsp = (uint32_t)&Context->Arguments[0];
    Context->UserSs  = StackSegment;

    // Setup arguments
    Context->Arguments[0] = 0;  // Return address
    Context->Arguments[1] = Argument;
}

Context_t*
ContextCreate(
    _In_ int    contextType,
    _In_ size_t contextSize)
{
	OsStatus_t           status;
	uintptr_t            physicalContextAddress;
    uintptr_t            contextAddress = 0;
	SystemMemorySpace_t* memorySpace    = GetCurrentMemorySpace();

	// Select proper segments based on context type and run-mode
	if (contextType == THREADING_CONTEXT_LEVEL0) {
		// Return a pointer to (STACK_TOP - SIZEOF(CONTEXT))
		// Reserve space for size + guard page
		status = MemorySpaceMapReserved(memorySpace, &contextAddress,
			contextSize + PAGE_SIZE, 0, MAPPING_VIRTUAL_GLOBAL);
		if (status != OsSuccess) {
			return NULL;
		}
		
		// Fixup contextAddress and commit size to memory
		status = MemorySpaceCommit(memorySpace, contextAddress + PAGE_SIZE,
			&physicalContextAddress, contextSize, 0);
		if (status != OsSuccess) {
			MemorySpaceUnmap(memorySpace, contextAddress, contextSize + PAGE_SIZE);
			return NULL;
		}
		
		contextAddress += (PAGE_SIZE + contextSize) - sizeof(Context_t);
	}
    else if (contextType == THREADING_CONTEXT_LEVEL1 || contextType == THREADING_CONTEXT_SIGNAL) {
    	// For both level1 and signal, we return a pointer to STACK_TOP_PAGE_BOTTOM
        if (contextType == THREADING_CONTEXT_LEVEL1) {
		    contextAddress = MEMORY_LOCATION_RING3_STACK_START - PAGE_SIZE;
        }
        else {
		    contextAddress = MEMORY_SEGMENT_SIGSTACK_BASE + MEMORY_SEGMENT_SIGSTACK_SIZE - PAGE_SIZE;
        }
        
        status = MemorySpaceCommit(memorySpace, contextAddress,
        	&physicalContextAddress, contextSize, 0);
        if (status != OsSuccess) {
        	return NULL;
        }
    }
	else {
		FATAL(FATAL_SCOPE_KERNEL, "ContextCreate::INVALID ContextType(%" PRIiIN ")", contextType);
	}
	return (Context_t*)contextAddress;
}

void
ContextDestroy(
    _In_ Context_t* context,
    _In_ int        contextType,
    _In_ size_t     contextSize)
{
    if (!context) {
        return;
    }
    
    // If it is kernel space contexts they are allocated in the kernel space,
    // otherwise they are cleaned up by the memory space
    if (contextType == THREADING_CONTEXT_LEVEL0) {
        uintptr_t contextAddress = (uintptr_t)context + sizeof(Context_t);
        MemorySpaceUnmap(GetCurrentMemorySpace(), 
        	contextAddress - (contextSize + PAGE_SIZE),
            contextSize + PAGE_SIZE);
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

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
 * X86-32 Thread Contexts
 */

#define __MODULE "context"
//#define __TRACE

#include <assert.h>
#include <arch/x86/cpu.h>
#include <arch/x86/memory.h>
#include <arch/x86/x32/gdt.h>
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
__PushRegister(
	_In_ uintptr_t* StackReference,
	_In_ uintptr_t  Value)
{
	*StackReference -= sizeof(uint32_t);
	*((uintptr_t*)(*StackReference)) = Value;
}

static void
__PushContextOntoStack(
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
		TemporaryStack, Address, Context->Eip);
	
	// On the previous stack, we would like to keep the Rip as it will be activated
	// before jumping to the previous address
	if (!TemporaryStack) {
        __PushRegister(&Context->UserEsp, Context->Eip);
		
		NewStackPointer = Context->UserEsp;
        __PushContextOntoStack(&NewStackPointer, Context);
	}
	else {
		NewStackPointer = TemporaryStack;

        __PushRegister(&Context->UserEsp, Context->Eip);
        __PushContextOntoStack(&NewStackPointer, Context);
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
ArchThreadContextReset(
    _In_ Context_t* context,
    _In_ int        contextType,
    _In_ uintptr_t  address,
    _In_ uintptr_t  argument)
{
    uint32_t dataSegment  = 0;
    uint32_t extraSegment = 0;
    uint32_t codeSegment  = 0;
    uint32_t stackSegment = 0;
    uint32_t ebpInitial   = (uint32_t)context + sizeof(Context_t);
    
	// Reset context
    memset(context, 0, sizeof(Context_t));

    if (contextType == THREADING_CONTEXT_LEVEL0) {
        codeSegment  = GDT_KCODE_SEGMENT;
        dataSegment  = GDT_KDATA_SEGMENT;
        extraSegment = GDT_KDATA_SEGMENT;
        stackSegment = GDT_KDATA_SEGMENT;
    }
    else if (contextType == THREADING_CONTEXT_LEVEL1 || contextType == THREADING_CONTEXT_SIGNAL) {
        extraSegment = GDT_EXTRA_SEGMENT + 0x03;
        codeSegment  = GDT_UCODE_SEGMENT + 0x03;
        stackSegment = GDT_UDATA_SEGMENT + 0x03;
        dataSegment  = GDT_UDATA_SEGMENT + 0x03;

		// Either initialize the ring3 stuff or zero out the values
	    context->Eax = CONTEXT_RESET_IDENTIFIER;
    }
    else {
        FATAL(FATAL_SCOPE_KERNEL, "ArchThreadContextCreate::INVALID ContextType(%" PRIiIN ")", contextType);
    }

    // Setup segments for the stack
    context->Ds  = dataSegment;
    context->Es  = dataSegment;
    context->Fs  = dataSegment;
    context->Gs  = extraSegment;
    context->Ebp = ebpInitial;

    // Setup entry, eflags and the code segment
    context->Eip     = address;
    context->Eflags  = CPU_EFLAGS_DEFAULT;
    context->Cs      = codeSegment;
    context->UserEsp = (uint32_t)&context->Arguments[0];
    context->UserSs  = stackSegment;

    // Setup arguments
    context->Arguments[0] = 0;  // Return address
    context->Arguments[1] = argument;
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
    WRITELINE("EAX: 0x%" PRIxIN ", EBX 0x%" PRIxIN ", ECX 0x%" PRIxIN ", EDX 0x%" PRIxIN "",
              context->Eax, context->Ebx, context->Ecx, context->Edx);

    // Dump stack registers
    WRITELINE("ESP 0x%" PRIxIN " (UserESP 0x%" PRIxIN "), EBP 0x%" PRIxIN ", Flags 0x%" PRIxIN "",
              context->Esp, context->UserEsp, context->Ebp, context->Eflags);
        
    // Dump copy registers
    WRITELINE("ESI 0x%" PRIxIN ", EDI 0x%" PRIxIN "", context->Esi, context->Edi);

    // Dump segments
    WRITELINE("CS 0x%" PRIxIN ", DS 0x%" PRIxIN ", GS 0x%" PRIxIN ", ES 0x%" PRIxIN ", FS 0x%" PRIxIN "",
              context->Cs, context->Ds, context->Gs, context->Es, context->Fs);

    // Dump IRQ information
    WRITELINE("IRQ 0x%" PRIxIN ", ErrorCode 0x%" PRIxIN ", UserSS 0x%" PRIxIN "",
              context->Irq, context->ErrorCode, context->UserSs);
    return OsSuccess;
}

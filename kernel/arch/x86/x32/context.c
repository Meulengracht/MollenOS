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
    } else if (contextType == THREADING_CONTEXT_LEVEL1 || contextType == THREADING_CONTEXT_SIGNAL) {
        extraSegment = GDT_EXTRA_SEGMENT + 0x03;
        codeSegment  = GDT_UCODE_SEGMENT + 0x03;
        stackSegment = GDT_UDATA_SEGMENT + 0x03;
        dataSegment  = GDT_UDATA_SEGMENT + 0x03;

		// Either initialize the ring3 stuff or zero out the values
	    context->Eax = CONTEXT_RESET_IDENTIFIER;
    } else {
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

static void
__GetContextFlags(
        _In_  int           contextType,
        _Out_ unsigned int* placementFlagsOut,
        _Out_ unsigned int* memoryFlagsOut)
{
    unsigned int   placementFlags = 0;
    unsigned int   memoryFlags    = MAPPING_DOMAIN | MAPPING_GUARDPAGE;

    if (contextType == THREADING_CONTEXT_LEVEL0) {
        placementFlags = MAPPING_VIRTUAL_GLOBAL;
    } else if (contextType == THREADING_CONTEXT_LEVEL1 || contextType == THREADING_CONTEXT_SIGNAL) {
        placementFlags = MAPPING_VIRTUAL_THREAD;
        memoryFlags |= MAPPING_USERSPACE;
    }
    else {
        FATAL(FATAL_SCOPE_KERNEL, "__GetContextFlags invalid contextType=%i", contextType);
    }

    *placementFlagsOut = placementFlags;
    *memoryFlagsOut = memoryFlags;
}

static oserr_t
__AllocateStackInMemory(
        _In_  unsigned int placementFlags,
        _In_  unsigned int memoryFlags,
        _In_  size_t       contextReservedSize,
        _In_  size_t       contextComittedSize,
        _Out_ uintptr_t*   contextAddressOut)
{
    MemorySpace_t* memorySpace = GetCurrentMemorySpace();
    vaddr_t        contextAddress;
    paddr_t        contextPhysicalAddress;
    oserr_t        oserr;

    // Return a pointer to (STACK_TOP - SIZEOF(CONTEXT))
    oserr = MemorySpaceMapReserved(
            memorySpace,
            &contextAddress,
            contextReservedSize,
            memoryFlags,
            placementFlags
    );
    if (oserr != OS_EOK) {
        return oserr;
    }

    // Adjust pointer to top of stack and then commit the first stack page
    oserr = MemorySpaceCommit(
            memorySpace,
            contextAddress + (contextReservedSize - contextComittedSize),
            &contextPhysicalAddress,
            contextComittedSize,
            0,
            0
    );
    if (oserr != OS_EOK) {
        MemorySpaceUnmap(memorySpace, contextAddress, contextReservedSize);
    }
    *contextAddressOut = contextAddress;
    return oserr;
}

Context_t*
ArchThreadContextIdle(void)
{
    Context_t*  idleContext;
    Context_t** currentStack = &idleContext;
    uintptr_t   stackAligned;

    // The way we currently calculate the stack-top is by block-aligning it. We expect
    // the stack to be block-aligned, and this probably *does not hold true* for all architectures.
    // For our current architectures we expect this
    stackAligned = (uintptr_t)currentStack & PAGE_MASK;
    stackAligned += PAGE_SIZE;
    stackAligned -= sizeof(Context_t);
    return (Context_t*)stackAligned;
}

Context_t*
ArchThreadContextCreate(
    _In_ int    contextType,
    _In_ size_t contextSize)
{
    oserr_t      oserr;
    uintptr_t    contextAddress;
    unsigned int placementFlags;
    unsigned int memoryFlags;

    __GetContextFlags(contextType, &placementFlags, &memoryFlags);
    oserr = __AllocateStackInMemory(
            placementFlags,
            memoryFlags,
            contextSize,
            PAGE_SIZE,
            &contextAddress
    );
    if (oserr != OS_EOK) {
        return NULL;
    }

    contextAddress += contextSize - sizeof(Context_t);
    return (Context_t*)contextAddress;
}

static uintptr_t
__FixupAddress(
        _In_ uintptr_t address,
        _In_ uintptr_t originalStackTop,
        _In_ uintptr_t newStackTop)
{
    uintptr_t offset = originalStackTop - address;
    return newStackTop - offset;
}

oserr_t
ArchThreadContextFork(
        _In_  Context_t*  baseContext,
        _In_  Context_t*  returnContext,
        _In_  int         contextType,
        _In_  size_t      contextSize,
        _Out_ Context_t** baseContextOut,
        _Out_ Context_t** contextOut)
{
    oserr_t      oserr;
    uintptr_t    contextAddress;
    Context_t*   newContext;
    uintptr_t    stackTop   = (uintptr_t)baseContext + sizeof(Context_t);
    uintptr_t    newStackTop;
    uintptr_t    stack      = CONTEXT_SP(returnContext);
    size_t       stackUsage = stackTop - stack;
    unsigned int placementFlags;
    unsigned int memoryFlags;
    TRACE("ArchThreadContextFork(stackTop=0x%x, stack=0x%x, stackUsage=0x%x)",
          stackTop, stack, stackUsage);

    __GetContextFlags(contextType, &placementFlags, &memoryFlags);

    // Commit enough space for the extra Context_t we add below
    oserr = __AllocateStackInMemory(
            placementFlags,
            memoryFlags,
            contextSize,
            (stackUsage + sizeof(Context_t)),
            &contextAddress
    );
    if (oserr != OS_EOK) {
        return oserr;
    }

    // contextAddress now points to the bottom of the stack, but we need it
    // to point to the top, so we can calculate a new 'current'. So let's fix
    // it up and then calculate new addresses
    contextAddress += contextSize;    // point to top
    newStackTop    = contextAddress;  // save it for fix ups
    contextAddress -= stackUsage;     // point to bottom

    // Now we copy from <stack> to <contextAddress>
    memcpy((void*)contextAddress, (const void*)stack, stackUsage);

    // And then, finally, we must set up a new context on top of this, which when unwound
    // somehow lands us back at the caller function. This is where the <returnContext> comes
    // into play, except we need to fix up the stack pointers in it
    contextAddress -= sizeof(Context_t);
    memcpy((void*)contextAddress, returnContext, sizeof(Context_t));

    // Fix up the stack pointers, which must not be their original values
    newContext = (Context_t*)contextAddress;
    newContext->Ebp = __FixupAddress(returnContext->Ebp, stackTop, newStackTop);
    newContext->Esp = __FixupAddress(returnContext->Esp, stackTop, newStackTop);

    // Fixup IRQ values which definitely needs to be reset. It's important that the entire Context_t is
    // consumed, otherwise we will be left with values on the stack, messing up the original stack.
    // TODO: this works on bochs, does it work on real HW?
    newContext->Cs      = GDT_KCODE_SEGMENT;
    newContext->UserEsp = (uint64_t)&newContext->Arguments[5]; // point stack pointer beyond the structure
    newContext->UserSs  = GDT_KDATA_SEGMENT;

    *baseContextOut = (Context_t*)(newStackTop - sizeof(Context_t));
    *contextOut = newContext;
    return OS_EOK;
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

    TRACE("ArchThreadContextDestroy(context=0x%llx, size=0x%llx)", context, contextSize);

    // adjust for size of context_t and then adjust back to base address
    contextAddress  = (uintptr_t)context;
    contextAddress += sizeof(Context_t);
    contextAddress -= contextSize;

    MemorySpaceUnmap(
            GetCurrentMemorySpace(),
            contextAddress,
            contextSize
    );
}

oserr_t
ArchThreadContextDump(
    _In_ Context_t* context)
{
    // Dump general registers
    DEBUG("EAX: 0x%" PRIxIN ", EBX 0x%" PRIxIN ", ECX 0x%" PRIxIN ", EDX 0x%" PRIxIN "",
          context->Eax, context->Ebx, context->Ecx, context->Edx);

    // Dump stack registers
    DEBUG("ESP 0x%" PRIxIN " (UserESP 0x%" PRIxIN "), EBP 0x%" PRIxIN ", Flags 0x%" PRIxIN "",
          context->Esp, context->UserEsp, context->Ebp, context->Eflags);
        
    // Dump copy registers
    DEBUG("ESI 0x%" PRIxIN ", EDI 0x%" PRIxIN "", context->Esi, context->Edi);

    // Dump segments
    DEBUG("CS 0x%" PRIxIN ", DS 0x%" PRIxIN ", GS 0x%" PRIxIN ", ES 0x%" PRIxIN ", FS 0x%" PRIxIN "",
          context->Cs, context->Ds, context->Gs, context->Es, context->Fs);

    // Dump IRQ information
    DEBUG("IRQ 0x%" PRIxIN ", ErrorCode 0x%" PRIxIN ", UserSS 0x%" PRIxIN "",
          context->Irq, context->ErrorCode, context->UserSs);
    return OS_EOK;
}

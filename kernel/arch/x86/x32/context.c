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
 * MollenOS X86-32 Thread Contexts
 */
#define __MODULE		"CTXT"
//#define __TRACE

/* Includes 
 * - System */
#include <os/context.h>
#include <threading.h>
#include <thread.h>
#include <memory.h>
#include <debug.h>
#include <heap.h>
#include <gdt.h>

/* Includes
 * - Library */
#include <string.h>
#include <stdio.h>

/* ContextCreate
 * Stack manipulation / setup of stacks for given
 * threading. We need functions that create a new kernel
 * stack and user/driver stack. Pass threading flags */
Context_t*
ContextCreate(
    _In_ Flags_t    ThreadFlags,
    _In_ int        ContextType,
	_In_ uintptr_t  EntryAddress)
{
	// Variables
	Context_t *Context       = NULL;
    uint32_t DataSegment     = 0,
             ExtraSegment    = 0,
             CodeSegment     = 0, 
             StackSegment    = 0;
    uintptr_t ContextAddress = 0, 
              EbpInitial     = 0;

	// Trace
	TRACE("ContextCreate(ThreadFlags 0x%x, Type %i, Eip 0x%x, Args 0x%x)",
		ThreadFlags, ContextType, EntryAddress);

	// Select proper segments based on context type and run-mode
	if (ContextType == THREADING_CONTEXT_LEVEL0 || ContextType == THREADING_CONTEXT_SIGNAL0) {
		ContextAddress  = ((uintptr_t)kmalloc_a(0x1000)) + 0x1000 - sizeof(Context_t);
		CodeSegment     = GDT_KCODE_SEGMENT;
		ExtraSegment    = StackSegment = DataSegment = GDT_KDATA_SEGMENT;
		EbpInitial      = (ContextAddress + sizeof(Context_t));
	}
    else if (ContextType == THREADING_CONTEXT_LEVEL1 || ContextType == THREADING_CONTEXT_SIGNAL1) {
        if (ContextType == THREADING_CONTEXT_LEVEL1) {
		    ContextAddress  = (MEMORY_LOCATION_RING3_STACK_START - sizeof(Context_t));
        }
        else {
		    ContextAddress  = ((MEMORY_SEGMENT_SIGSTACK_BASE + MEMORY_SEGMENT_SIGSTACK_SIZE) - sizeof(Context_t));
        }
 		EbpInitial      = MEMORY_LOCATION_RING3_STACK_START;
        ExtraSegment    = GDT_EXTRA_SEGMENT + 0x03;

        // Now select the correct run-mode segments
        if (THREADING_RUNMODE(ThreadFlags) == THREADING_DRIVERMODE) {
            CodeSegment     = GDT_PCODE_SEGMENT + 0x03;
		    StackSegment    = DataSegment = GDT_PDATA_SEGMENT + 0x03;
        }
        else if (THREADING_RUNMODE(ThreadFlags) == THREADING_USERMODE) {
            CodeSegment     = GDT_UCODE_SEGMENT + 0x03;
		    StackSegment    = DataSegment = GDT_UDATA_SEGMENT + 0x03;
        }
        else {
            FATAL(FATAL_SCOPE_KERNEL, "ContextCreate::INVALID THREADFLAGS(%u)", ThreadFlags);
        }

        // Map in the context
        if (!AddressSpaceGetMapping(AddressSpaceGetCurrent(), ContextAddress)) {
            AddressSpaceMap(AddressSpaceGetCurrent(), NULL, &ContextAddress, PAGE_SIZE,
                ASPACE_FLAG_APPLICATION | ASPACE_FLAG_SUPPLIEDVIRTUAL, __MASK);
        }
    }
	else {
		FATAL(FATAL_SCOPE_KERNEL, "ContextCreate::INVALID ContextType(%i)", ContextType);
	}

	// Initialize the context pointer
	Context = (Context_t*)ContextAddress;

	// Setup segments for the stack
	Context->Ds = DataSegment;
	Context->Es = DataSegment;
	Context->Fs = DataSegment;
	Context->Gs = ExtraSegment;

	// Initialize registers to zero value
	Context->Eax = 0;
	Context->Ebx = 0;
	Context->Ecx = 0;
	Context->Edx = 0;
	Context->Esi = 0;
	Context->Edi = 0;
	Context->Ebp = EbpInitial;
	Context->Esp = 0;

	// Initialize irq/error code for
	// interrupt values
	Context->Irq = 0;
	Context->ErrorCode = 0;

	// Setup entry, eflags and the code segment
	Context->Eip = EntryAddress;
	Context->Eflags = X86_THREAD_EFLAGS;
	Context->Cs = CodeSegment;

	// Either initialize the ring3 stuff
	// or zero out the values
    Context->UserEsp = (uintptr_t)&Context->Arguments[0];
    Context->UserSs = StackSegment;

	// Return the newly created context
	return Context;
}

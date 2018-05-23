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
#include <log.h>
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
	_In_ uintptr_t  EntryAddress,
    _In_ uintptr_t  ReturnAddress,
    _In_ uintptr_t  Argument0,
    _In_ uintptr_t  Argument1)
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
    memset(Context, 0, sizeof(Context_t));

	// Setup segments for the stack
	Context->Ds = DataSegment;
	Context->Es = DataSegment;
	Context->Fs = DataSegment;
	Context->Gs = ExtraSegment;

	// Initialize registers to zero value
	Context->Ebp = EbpInitial;

	// Setup entry, eflags and the code segment
	Context->Eip = EntryAddress;
	Context->Eflags = X86_THREAD_EFLAGS;
	Context->Cs = CodeSegment;

	// Either initialize the ring3 stuff
	// or zero out the values
    Context->UserEsp = (uintptr_t)&Context->Arguments[0];
    Context->UserSs = StackSegment;

    // Setup arguments
    Context->Arguments[0] = ReturnAddress;
    Context->Arguments[1] = Argument0;
    Context->Arguments[2] = Argument1;

	// Return the newly created context
	return Context;
}

/* ContextDump 
 * Dumps the contents of the given context for debugging */
OsStatus_t
ContextDump(
	_In_ Context_t *Context)
{
	// Dump general registers
	WRITELINE("EAX: 0x%x, EBX 0x%x, ECX 0x%x, EDX 0x%x",
		Context->Eax, Context->Ebx, Context->Ecx, Context->Edx);

	// Dump stack registers
	WRITELINE("ESP 0x%x (UserESP 0x%x), EBP 0x%x, Flags 0x%x",
        Context->Esp, Context->UserEsp, Context->Ebp, Context->Eflags);
        
    // Dump copy registers
	WRITELINE("ESI 0x%x, EDI 0x%x", Context->Esi, Context->Edi);

	// Dump segments
	WRITELINE("CS 0x%x, DS 0x%x, GS 0x%x, ES 0x%x, FS 0x%x",
		Context->Cs, Context->Ds, Context->Gs, Context->Es, Context->Fs);

	// Dump IRQ information
	WRITELINE("IRQ 0x%x, ErrorCode 0x%x, UserSS 0x%x",
		Context->Irq, Context->ErrorCode, Context->UserSs);
	return OsSuccess;
}

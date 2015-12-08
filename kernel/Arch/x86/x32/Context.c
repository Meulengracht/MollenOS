/* MollenOS
*
* Copyright 2011 - 2014, Philip Meulengracht
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

/* Includes */
#include <Arch.h>
#include <Scheduler.h>
#include <Thread.h>
#include <Memory.h>
#include <Gdt.h>
#include <Heap.h>
#include <string.h>
#include <stdio.h>

Registers_t *ContextCreate(Addr_t Eip)
{
	Registers_t *context;
	Addr_t context_location;

	/* Allocate a new context */
	context_location = (Addr_t)kmalloc_a(0x1000) + 0x1000 - 0x4 - sizeof(Registers_t);
	context = (Registers_t*)context_location;

	/* Set Segments */
	context->Ds = X86_KERNEL_DATA_SEGMENT;
	context->Fs = X86_KERNEL_DATA_SEGMENT;
	context->Es = X86_KERNEL_DATA_SEGMENT;
	context->Gs = X86_KERNEL_DATA_SEGMENT;

	/* Initialize Registers */
	context->Eax = 0;
	context->Ebx = 0;
	context->Ecx = 0;
	context->Edx = 0;
	context->Esi = 0;
	context->Edi = 0;
	context->Ebp = (context_location + sizeof(Registers_t));
	context->Esp = 0;

	/* Set NULL */
	context->Irq = 0;
	context->ErrorCode = 0;

	/* Set Entry */
	context->Eip = Eip;
	context->Eflags = X86_THREAD_EFLAGS;
	context->Cs = X86_KERNEL_CODE_SEGMENT;

	/* Null user stuff */
	context->UserEsp = 0;
	context->UserSs = 0;
	context->UserArg = 0;

	return context;
}

Registers_t *ContextUserCreate(Addr_t StackStartAddr, Addr_t Eip, Addr_t *Args)
{
	/* Context Ptr */
	Registers_t *uContext;

	/* Allocate a new context */
	uContext = (Registers_t*)(StackStartAddr - sizeof(Registers_t) - sizeof(Addr_t));

	/* Set Segments */
	uContext->Ds = X86_GDT_USER_DATA + 0x03;
	uContext->Fs = X86_GDT_USER_DATA + 0x03;
	uContext->Es = X86_GDT_USER_DATA + 0x03;
	uContext->Gs = X86_GDT_USER_DATA + 0x03;

	/* Initialize Registers */
	uContext->Eax = 0;
	uContext->Ebx = 0;
	uContext->Ecx = 0;
	uContext->Edx = 0;
	uContext->Esi = 0;
	uContext->Edi = 0;
	uContext->Ebp = (StackStartAddr - sizeof(Addr_t));
	uContext->Esp = 0;

	/* Set NULL */
	uContext->Irq = 0;
	uContext->ErrorCode = 0;

	/* Set Entry */
	uContext->Eip = Eip;
	uContext->Eflags = X86_THREAD_EFLAGS;
	uContext->Cs = X86_USER_CODE_SEGMENT + 0x03;

	/* Null user stuff */
	uContext->UserEsp = (Addr_t)&uContext->UserEsp;
	uContext->UserSs = X86_GDT_USER_DATA + 0x03;
	uContext->UserArg = (Addr_t)Args;

	/* Done! */
	return uContext;
}

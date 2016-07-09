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
	/* Variables */
	Registers_t *Context;
	Addr_t CtxLocation;

	/* Allocate a new context */
	CtxLocation = ((Addr_t)kmalloc_a(0x1000)) + 0x1000 - sizeof(Registers_t);
	Context = (Registers_t*)CtxLocation;

	/* Set Segments */
	Context->Ds = X86_KERNEL_DATA_SEGMENT;
	Context->Fs = X86_KERNEL_DATA_SEGMENT;
	Context->Es = X86_KERNEL_DATA_SEGMENT;
	Context->Gs = X86_KERNEL_DATA_SEGMENT;

	/* Initialize Registers */
	Context->Eax = 0;
	Context->Ebx = 0;
	Context->Ecx = 0;
	Context->Edx = 0;
	Context->Esi = 0;
	Context->Edi = 0;
	Context->Ebp = (CtxLocation + sizeof(Registers_t));
	Context->Esp = 0;

	/* Set NULL */
	Context->Irq = 0;
	Context->ErrorCode = 0;

	/* Set Entry */
	Context->Eip = Eip;
	Context->Eflags = X86_THREAD_EFLAGS;
	Context->Cs = X86_KERNEL_CODE_SEGMENT;

	/* Null user stuff */
	Context->UserEsp = 0;
	Context->UserSs = 0;
	Context->UserArg = 0;

	/* Done! */
	return Context;
}

Registers_t *ContextUserCreate(Addr_t StackStartAddr, Addr_t Eip, Addr_t *Args)
{
	/* Context Ptr */
	Registers_t *uContext;

	/* Allocate a new context */
	uContext = (Registers_t*)(StackStartAddr - sizeof(Registers_t));

	/* Set Segments */
	uContext->Ds = X86_USER_DATA_SEGMENT + 0x03;
	uContext->Fs = X86_USER_DATA_SEGMENT + 0x03;
	uContext->Es = X86_USER_DATA_SEGMENT + 0x03;
	uContext->Gs = X86_USER_DATA_SEGMENT + 0x03;

	/* Initialize Registers */
	uContext->Eax = 0;
	uContext->Ebx = 0;
	uContext->Ecx = 0;
	uContext->Edx = 0;
	uContext->Esi = 0;
	uContext->Edi = 0;
	uContext->Ebp = StackStartAddr;
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
	uContext->UserSs = X86_USER_DATA_SEGMENT + 0x03;
	uContext->UserArg = (Addr_t)Args;

	/* Done! */
	return uContext;
}

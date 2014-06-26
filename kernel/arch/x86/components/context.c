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
#include <arch.h>
#include <scheduler.h>
#include <thread.h>
#include <memory.h>
#include <gdt.h>
#include <heap.h>
#include <string.h>
#include <stdio.h>

registers_t *context_create(addr_t eip)
{
	registers_t *context;
	addr_t context_location;

	/* Allocate a new context */
	context_location = (addr_t)kmalloc_a(0x1000) + 0x1000 - 0x4 - sizeof(registers_t);
	context = (registers_t*)context_location;

	/* Set Segments */
	context->ds = X86_KERNEL_DATA_SEGMENT;
	context->fs = X86_KERNEL_DATA_SEGMENT;
	context->es = X86_KERNEL_DATA_SEGMENT;
	context->gs = X86_KERNEL_DATA_SEGMENT;

	/* Initialize Registers */
	context->eax = 0;
	context->ebx = 0;
	context->ecx = 0;
	context->edx = 0;
	context->esi = 0;
	context->edi = 0;
	context->ebp = (context_location + sizeof(registers_t));
	context->esp = 0;

	/* Set NULL */
	context->irq = 0;
	context->error_code = 0;

	/* Set Entry */
	context->eip = eip;
	context->eflags = X86_THREAD_EFLAGS;
	context->cs = X86_KERNEL_CODE_SEGMENT;

	/* Null user stuff */
	context->user_esp = 0;
	context->user_ss = 0;
	context->user_arg = 0;

	return context;
}

registers_t *context_user_create(addr_t eip, addr_t *args)
{
	registers_t *context;
	addr_t context_location;

	/* Allocate a new context */
	context_location = (addr_t)kmalloc_a(0x1000) + 0x1000 - 0x4 - sizeof(registers_t);
	context = (registers_t*)context_location;

	/* Set Segments */
	context->ds = X86_GDT_USER_DATA + 0x03;
	context->fs = X86_GDT_USER_DATA + 0x03;
	context->es = X86_GDT_USER_DATA + 0x03;
	context->gs = X86_GDT_USER_DATA + 0x03;

	/* Initialize Registers */
	context->eax = 0;
	context->ebx = 0;
	context->ecx = 0;
	context->edx = 0;
	context->esi = 0;
	context->edi = 0;
	context->ebp = (context_location + sizeof(registers_t));
	context->esp = 0;

	/* Set NULL */
	context->irq = 0;
	context->error_code = 0;

	/* Set Entry */
	context->eip = eip;
	context->eflags = X86_THREAD_EFLAGS;
	context->cs = X86_USER_CODE_SEGMENT + 0x03;

	/* Null user stuff */
	context->user_esp = (addr_t)&context->user_esp;
	context->user_ss = X86_GDT_USER_DATA + 0x03;
	context->user_arg = (addr_t)args;

	return context;
}

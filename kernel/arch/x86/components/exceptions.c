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
* MollenOS x86-32 Exception Handlers & Init
*/

#include <arch.h>
#include <exceptions.h>
#include <idt.h>
#include <gdt.h>
#include <stdio.h>

void exceptions_init(void)
{
	/* Install exception handlers */
	idt_install_descriptor(0, (uint32_t)&irq_handler0, 
		X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(1, (uint32_t)&irq_handler1,
		X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(2, (uint32_t)&irq_handler2,
		X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(3, (uint32_t)&irq_handler3,
		X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(4, (uint32_t)&irq_handler4,
		X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(5, (uint32_t)&irq_handler5,
		X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(6, (uint32_t)&irq_handler6,
		X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(7, (uint32_t)&irq_handler7,
		X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(8, (uint32_t)&irq_handler8,
		X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(9, (uint32_t)&irq_handler9,
		X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(10, (uint32_t)&irq_handler10,
		X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(11, (uint32_t)&irq_handler11,
		X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(12, (uint32_t)&irq_handler12,
		X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(13, (uint32_t)&irq_handler13,
		X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(14, (uint32_t)&irq_handler14,
		X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(15, (uint32_t)&irq_handler15,
		X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(16, (uint32_t)&irq_handler16,
		X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(17, (uint32_t)&irq_handler17,
		X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(18, (uint32_t)&irq_handler18,
		X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(19, (uint32_t)&irq_handler19,
		X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(20, (uint32_t)&irq_handler20,
		X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(21, (uint32_t)&irq_handler21,
		X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(22, (uint32_t)&irq_handler22,
		X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(23, (uint32_t)&irq_handler23,
		X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(24, (uint32_t)&irq_handler24,
		X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(25, (uint32_t)&irq_handler25,
		X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(26, (uint32_t)&irq_handler26,
		X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(27, (uint32_t)&irq_handler27,
		X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(28, (uint32_t)&irq_handler28,
		X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(29, (uint32_t)&irq_handler29,
		X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(30, (uint32_t)&irq_handler30,
		X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	idt_install_descriptor(31, (uint32_t)&irq_handler31,
		X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
}

void exception_entry(registers_t *regs)
{
	/* Determine Irq */
	printf("Exception Handler! Irq %u, Error Code: %u, Faulty Address: 0x%x\n",
		regs->irq, regs->error_code, regs->eip);

	for(;;);
}

void kernel_panic(const char *message)
{
	printf("ASSERT PANIC: %s\n", message);
	printf("Fix this philip!\n");
	for (;;);
}
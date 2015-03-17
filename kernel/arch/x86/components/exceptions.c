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

#include <Arch.h>
#include <Exceptions.h>
#include <Idt.h>
#include <thread.h>
#include <gdt.h>
#include <string.h>
#include <stdio.h>
#include <stddef.h>
#include <heap.h>

/* Extern Assembly */
extern uint32_t __getcr2(void);
extern void init_fpu(void);
extern void load_fpu(Addr_t *buffer);
extern void clear_ts(void);

/* Extern Thread */
extern Thread_t *threading_get_current_thread(Cpu_t cpu);
extern Cpu_t get_cpu(void);

void ExceptionsInit(void)
{
	/* Install exception handlers */
	IdtInstallDescriptor(0, (uint32_t)&irq_handler0,
		X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(1, (uint32_t)&irq_handler1,
		X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(2, (uint32_t)&irq_handler2,
		X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(3, (uint32_t)&irq_handler3,
		X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(4, (uint32_t)&irq_handler4,
		X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(5, (uint32_t)&irq_handler5,
		X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(6, (uint32_t)&irq_handler6,
		X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(7, (uint32_t)&irq_handler7,
		X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(8, (uint32_t)&irq_handler8,
		X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(9, (uint32_t)&irq_handler9,
		X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(10, (uint32_t)&irq_handler10,
		X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(11, (uint32_t)&irq_handler11,
		X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(12, (uint32_t)&irq_handler12,
		X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(13, (uint32_t)&irq_handler13,
		X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(14, (uint32_t)&irq_handler14,
		X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(15, (uint32_t)&irq_handler15,
		X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(16, (uint32_t)&irq_handler16,
		X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(17, (uint32_t)&irq_handler17,
		X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(18, (uint32_t)&irq_handler18,
		X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(19, (uint32_t)&irq_handler19,
		X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(20, (uint32_t)&irq_handler20,
		X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(21, (uint32_t)&irq_handler21,
		X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(22, (uint32_t)&irq_handler22,
		X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(23, (uint32_t)&irq_handler23,
		X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(24, (uint32_t)&irq_handler24,
		X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(25, (uint32_t)&irq_handler25,
		X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(26, (uint32_t)&irq_handler26,
		X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(27, (uint32_t)&irq_handler27,
		X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(28, (uint32_t)&irq_handler28,
		X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(29, (uint32_t)&irq_handler29,
		X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(30, (uint32_t)&irq_handler30,
		X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
	IdtInstallDescriptor(31, (uint32_t)&irq_handler31,
		X86_KERNEL_CODE_SEGMENT, X86_IDT_RING3 | X86_IDT_PRESENT | X86_IDT_INTERRUPT_GATE32);
}

void ExceptionEntry(Registers_t *regs)
{
	Thread_t *t;
	Cpu_t cpu;
	uint32_t fixed = 0;
	//char *instructions = NULL;

	/* Determine Irq */
	if (regs->Irq == 7)
	{
		/* Device Not Available */
		/* This happens if FPU needs to be restored OR initialized */
		cpu = get_cpu();
		t = threading_get_current_thread(cpu);

		/* If it is NULL shit has gone down */
		if (t != NULL)
		{
			/* Now, do we need to initialise? */
			if (!(t->Flags & X86_THREAD_FPU_INITIALISED))
			{
				/* Clear TS */
				clear_ts();

				/* Init */
				init_fpu();
				
				/* Set this exception as handled */
				fixed = 1;
			}
			else if (!(t->Flags & X86_THREAD_USEDFPU))
			{
				/* Clear TS */
				clear_ts();

				/* Noooo, we just need to restore */
				load_fpu(t->FpuBuffer);

				/* Now, set thread to used fpu */
				t->Flags |= X86_THREAD_USEDFPU;

				/* Set this exception as handled */
				fixed = 1;
			}
		}
	}
	else if (regs->Irq == 14)
	{
		printf("CR2 Address: 0x%x... Faulty Address: 0x%x\n", __getcr2(), regs->Eip);
		idle();
	}

	if (fixed == 0)
	{
		printf("Exception Handler! Irq %u, Error Code: %u, Faulty Address: 0x%x\n",
			regs->Irq, regs->ErrorCode, regs->Eip);

		/* Disassembly */
		//instructions = get_instructions_at_mem(regs->eip);

		/* Print it */
		//printf("Disassembly of 0x%x:\n%s", regs->eip, instructions);

		idle();
	}
}

/* Disassembles Memory */
//char *get_instructions_at_mem(addr_t address)
//{
//	/* We debug 50 bytes of memory */
//	int n;
//	int num_instructions = 1; /* Debug, normal 50 */
//	char *instructions = (char*)kmalloc(0x1000);
//	addr_t pointer = address;
//
//	/* Null */
//	memset(instructions, 0, 0x1000);
//
//	/* Do it! */
//	for (n = 0; n < num_instructions; n++)
//	{
//		INSTRUCTION inst;
//		char inst_str[64];
//
//		/* Get instruction */
//		get_instruction(&inst, (void*)pointer, MODE_32);
//
//		/* Translate */
//		get_instruction_string(&inst, FORMAT_ATT, 0, inst_str, sizeof(inst_str));
//
//		/* Append to list */
//		if (n == 0)
//		{
//			strcpy(instructions, inst_str);
//			strcat(instructions, "\n");
//		}
//		else
//		{
//			strcat(instructions, inst_str);
//			strcat(instructions, "\n");
//		}
//
//		/* Increament Pointer */
//		pointer += inst.length;
//	}
//
//	return instructions;
//}

void kernel_panic(const char *message)
{
	printf("ASSERT PANIC: %s\n", message);
	printf("Fix this philip!\n");
	for (;;);
}
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

/* Includes */
#include <Threading.h>
#include <Thread.h>
#include <stdio.h>

/* Extern Assembly */
extern uint32_t __getcr2(void);
extern void init_fpu(void);
extern void load_fpu(Addr_t *buffer);
extern void clear_ts(void);

/* Common entry for all exceptions */
void ExceptionEntry(Registers_t *regs)
{
	/* We'll need these */
	MCoreThread_t *cThread;
	x86Thread_t *cThreadx86;
	Cpu_t Cpu;
	uint32_t IssueFixed = 0;
	//char *instructions = NULL;

	/* Determine Irq */
	if (regs->Irq == 7)
	{
		/* Device Not Available */
		
		/* This happens if FPU needs to be restored OR initialized */
		Cpu = ApicGetCpu();
		cThread = ThreadingGetCurrentThread(Cpu);

		/* If it is NULL shit has gone down */
		if (cThread != NULL)
		{
			/* Cast */
			cThreadx86 = (x86Thread_t*)cThread->ThreadData;

			/* Now, do we need to initialise? */
			if (!(cThreadx86->Flags & X86_THREAD_FPU_INITIALISED))
			{
				/* Clear TS */
				clear_ts();

				/* Init */
				init_fpu();
				
				/* Set this exception as handled */
				IssueFixed = 1;
			}
			else if (!(cThreadx86->Flags & X86_THREAD_USEDFPU))
			{
				/* Clear TS */
				clear_ts();

				/* Noooo, we just need to restore */
				load_fpu(cThreadx86->FpuBuffer);

				/* Now, set thread to used fpu */
				cThreadx86->Flags |= X86_THREAD_USEDFPU;

				/* Set this exception as handled */
				IssueFixed = 1;
			}
		}
	}
	else if (regs->Irq == 14)
	{
		/* Odd */
		printf("CR2 Address: 0x%x... Faulty Address: 0x%x\n", __getcr2(), regs->Eip);
		InterruptDisable();
		Idle();
	}

	if (IssueFixed == 0)
	{
		printf("Exception Handler! Irq %u, Error Code: %u, Faulty Address: 0x%x\n",
			regs->Irq, regs->ErrorCode, regs->Eip);

		/* Disassembly */
		//instructions = get_instructions_at_mem(regs->eip);

		/* Print it */
		//printf("Disassembly of 0x%x:\n%s", regs->eip, instructions);

		Idle();
	}
}

//EBP is passed to the exception handler by the CPU
void StackTrace(uint32_t MaxFrames)
{
	/* Get stack position */
	uint32_t *StackPtr = (uint32_t*)&MaxFrames;
	uint32_t Itr = MaxFrames;

	/* Run */
	while (Itr != 0)
	{
		/* Get IP */
		uint32_t Ip = StackPtr[2];

		/* Sanity */
		if (Ip == 0)
			break;

		/* We could lookup */
		printf("Call Stack: 0x%x\n", Ip);

		/* Get argument pointer */
		//uint32_t *ArgPtr = &StackPtr[0];

		/* Unwind to next ebp */
		StackPtr = (uint32_t*)StackPtr[1];
		StackPtr--;

		/* Dec */
		Itr--;
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
	printf("Thread %u!\n", ThreadingGetCurrentThreadId());
	printf("Stack Trace:\n");
	StackTrace(6);
	for (;;);
}
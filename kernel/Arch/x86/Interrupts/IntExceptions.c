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
#include "../Arch.h"
#include <ProcessManager.h>
#include <Threading.h>
#include <Thread.h>
#include <Modules/ModuleManager.h>
#include <Log.h>
#include <Heap.h>
#include "../Memory.h"
#include <stdio.h>

/* Extern Assembly */
extern uint32_t __getcr2(void);
extern void init_fpu(void);
extern void load_fpu(Addr_t *buffer);
extern void clear_ts(void);
extern void ThreadingDebugPrint(void);

/* Our very own Cdecl x86 stack trace :-) */
void StackTrace(size_t MaxFrames)
{
	/* Get stack position */
	uint32_t *StackPtr = (uint32_t*)&MaxFrames;
	size_t Itr = MaxFrames;

	/* Run */
	while (Itr != 0)
	{
		/* Get IP */
		uint32_t Ip = StackPtr[2];

		/* Sanity */
		if (Ip == 0)
			break;

		/* We could lookup */
		if (Ip >= MEMORY_LOCATION_MODULES
			&& Ip < (MEMORY_LOCATION_MODULES + 0x1000000))
		{
			/* Try to find the module */
			MCoreModule_t *Module = ModuleFindAddress(Ip);

			/* Sanity */
			if (Module != NULL)
			{
				uint32_t Diff = Ip - Module->Descriptor->BaseVirtual;
				LogInformation("CSTK", "%u - 0x%x (%s)",
					MaxFrames - Itr, Diff, Module->Header->ModuleName);
			}
		}
		else
			LogInformation("CSTK", "%u - 0x%x", MaxFrames - Itr, Ip);


		/* Get argument pointer */
		//uint32_t *ArgPtr = &StackPtr[0];

		/* Unwind to next ebp */
		StackPtr = (uint32_t*)StackPtr[1];
		StackPtr--;

		/* Dec */
		Itr--;
	}
}

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
		/* Get failed address */
		Addr_t UnmappedAddr = (Addr_t)__getcr2();

		/* Driver Address? */
		if (UnmappedAddr >= MEMORY_LOCATION_RESERVED
			&& UnmappedAddr < MEMORY_LOCATION_USER_ARGS)
		{
			/* .. Probably, lets check */
			Addr_t Physical = IoSpaceValidate(UnmappedAddr);
			if (Physical != 0)
			{
				/* Map it (disable caching for IoSpaces) */
				MmVirtualMap(NULL, (Physical & PAGE_MASK), 
					(UnmappedAddr & PAGE_MASK), PAGE_CACHE_DISABLE);

				/* Issue is fixed */
				IssueFixed = 1;
			}
		}

		/* Kernel heap address? */
		else if (UnmappedAddr >= MEMORY_LOCATION_HEAP
			&& UnmappedAddr < MEMORY_LOCATION_HEAP_END)
		{
			/* Yes, validate it in the heap */
			if (!HeapValidateAddress(NULL, UnmappedAddr)) 
			{
				/* Map in in */
				MmVirtualMap(NULL, MmPhysicalAllocateBlock(), (UnmappedAddr & PAGE_MASK), 0);

				/* Issue is fixed */
				IssueFixed = 1;
			}
		}

		/* User heap address? */
		else if (UnmappedAddr >= MEMORY_LOCATION_USER_HEAP
			&& UnmappedAddr < MEMORY_LOCATION_USER_GUARD)
		{
			/* Get heap */
			Cpu_t CurrentCpu = ApicGetCpu();
			MCoreProcess_t *Process =
				PmGetProcess(ThreadingGetCurrentThread(CurrentCpu)->ProcessId);

			/* Sanity */
			if (Process != NULL)
			{
				/* Yes, validate it in the heap */
				if (!HeapValidateAddress(Process->Heap, UnmappedAddr))
				{
					/* Map in in */
					MmVirtualMap(NULL, MmPhysicalAllocateBlock(), 
						(UnmappedAddr & PAGE_MASK), PAGE_USER);

					/* Issue is fixed */
					IssueFixed = 1;
				}
			}
		}

		/* User stack address? */
		else if (UnmappedAddr >= (MEMORY_LOCATION_USER_GUARD + PAGE_SIZE)
			&& UnmappedAddr < MEMORY_LOCATION_USER_STACK)
		{
			/* Map in in */
			MmVirtualMap(NULL, MmPhysicalAllocateBlock(), (UnmappedAddr & PAGE_MASK), PAGE_USER);

			/* Issue is fixed */
			IssueFixed = 1;
		}
		
		/* Sanity */
		if (IssueFixed == 0)
		{
			/* Get cpu */
			Cpu = ApicGetCpu();

			/* Enable log */
			LogRedirect(LogConsole);

			/* Odd */
			printf("CR2 Address: 0x%x\n", UnmappedAddr);

			/* We could lookup */
			if (regs->Eip >= MEMORY_LOCATION_MODULES
				&& regs->Eip < (MEMORY_LOCATION_MODULES + 0x1000000))
			{
				/* Try to find the module */
				MCoreModule_t *Module = ModuleFindAddress(regs->Eip);

				/* Sanity */
				if (Module != NULL) {
					size_t Diff = regs->Eip - Module->Descriptor->BaseVirtual;
					printf("Fauly Address: 0x%x (%s)\n", Diff, Module->Header->ModuleName);
				}
			}
			else {
				printf("Faulty Address: 0x%x\n", regs->Eip);
				printf("Stack ptr: 0x%x\n", regs->Esp);
				StackTrace(3);
			}

			/* Log Thread Information */
			LogFatal("SYST", "Thread %s - %u (Core %u)!",
				ThreadingGetCurrentThread(Cpu)->Name,
				ThreadingGetCurrentThreadId(), Cpu);
			ThreadingDebugPrint();

			for (;;);
		}
	}

	if (IssueFixed == 0)
	{
		/* Enable log */
		LogRedirect(LogConsole);

		printf("Exception Handler! Irq %u, Error Code: %u, Faulty Address: 0x%x\n",
			regs->Irq, regs->ErrorCode, regs->Eip);

		/* Disassembly */
		//instructions = get_instructions_at_mem(regs->eip);

		/* Print it */
		//printf("Disassembly of 0x%x:\n%s", regs->eip, instructions);

		Idle();
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

/* The kernel panic, spits out debug info */
void kernel_panic(const char *Message)
{
	/* Get Cpu */
	Cpu_t CurrCpu = ApicGetCpu();

	/* Enable log */
	LogRedirect(LogConsole);

	/* Print */
	LogFatal("SYST", Message);

	/* Try to stack trace first */
	StackTrace(6);

	/* Log Thread Information */
	LogFatal("SYST", "Thread %s - %u (Core %u)!", 
		ThreadingGetCurrentThread(CurrCpu)->Name, 
		ThreadingGetCurrentThreadId(), CurrCpu);
	ThreadingDebugPrint();

	/* Gooodbye */
	InterruptDisable();
	for (;;);
}
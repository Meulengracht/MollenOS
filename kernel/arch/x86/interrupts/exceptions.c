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
* TODO:
* Cleanup
* Move debugging functions to <debug.h>
*/

/* Includes */
#include "../Arch.h"
#include <process/phoenix.h>
#include <modules/modules.h>
#include <Threading.h>
#include <Thread.h>
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
extern void enter_thread(Registers_t *Regs);

/* Use this to lookup the faulty module or process */
void LookupFault(Addr_t Address, Addr_t *Base, char **Name)
{
	if (Address >= MEMORY_LOCATION_USER
		&& Address < MEMORY_LOCATION_USER_HEAP)
	{
		/* Get current process information */
		MCoreAsh_t *Ash = PhoenixGetAsh(PHOENIX_CURRENT);

		/* Sanitize the lookup */
		if (Ash != NULL
			&& Ash->Executable != NULL) {
			char *PmName = (char*)MStringRaw(Ash->Executable->Name);
			Addr_t PmBase = Ash->Executable->VirtualAddress;

			/* Iterate libraries to find the sinner
			 * We only want one */
			if (Ash->Executable->LoadedLibraries != NULL) {
				foreach(lNode, Ash->Executable->LoadedLibraries) {
					MCorePeFile_t *Lib = (MCorePeFile_t*)lNode->Data;
					if (Address >= Lib->VirtualAddress) {
						PmName = (char*)MStringRaw(Lib->Name);
						PmBase = Lib->VirtualAddress;
					}
				}
			}

			/* Update */
			*Base = PmBase;
			*Name = PmName;
			return;
		}
	}

	/* Otherwise null */
	*Base = 0;
	*Name = NULL;
}

/* Our very own Cdecl x86 stack trace :-) */
void StackTrace(size_t MaxFrames)
{
	/* Get stack position */
	uint32_t *StackPtr = (uint32_t*)&MaxFrames;
	size_t Itr = MaxFrames;

	/* Run */
	while (Itr != 0 && StackPtr != NULL)
	{
		/* Variables */
		uint32_t Ip = StackPtr[2];
		Addr_t Base = 0;
		char *Name = NULL;

		/* Sanity */
		if (Ip == 0)
			break;

		/* Who did this? */
		LookupFault(Ip, &Base, &Name);

		/* Did we find the sinner? */
		if (Name != NULL && Base != 0) {
			Addr_t Diff = Ip - Base;
			LogInformation("CSTK", "%u - 0x%x (%s)",
				MaxFrames - Itr, Diff, Name);
		}
		else {
			LogInformation("CSTK", "%u - 0x%x", MaxFrames - Itr, Ip);
		}

		/* Sanity 
		 * Always do null-checks */
		if (StackPtr[1] == 0) {
			break;
		}

		/* Get argument pointer */
		//uint32_t *ArgPtr = &StackPtr[0];

		/* Unwind to next ebp */
		StackPtr = (uint32_t*)StackPtr[1];
		StackPtr--;

		/* Sanitize */
		if (!AddressSpaceGetMap(AddressSpaceGetCurrent(), (VirtAddr_t)StackPtr)) 
			break;

		/* Dec */
		Itr--;
	}
}

void MemoryDump(char *desc, void *addr, int len) {
	int i;
	unsigned char buff[17];
	unsigned char *pc = (unsigned char*)addr;

	// Output description if given.
	if (desc != NULL)
		printf("%s:\n", desc);

	if (len == 0) {
		printf("  ZERO LENGTH\n");
		return;
	}
	if (len < 0) {
		printf("  NEGATIVE LENGTH: %i\n", len);
		return;
	}

	// Process every byte in the data.
	for (i = 0; i < len; i++) {
		// Multiple of 16 means new line (with line offset).

		if ((i % 16) == 0) {
			// Just don't print ASCII for the zeroth line.
			if (i != 0)
				printf("  %s\n", buff);

			// Output the offset.
			printf("  %04x ", i);
		}

		// Now the hex code for the specific character.
		printf(" %02x", pc[i]);

		// And store a printable ASCII character for later.
		if ((pc[i] < 0x20) || (pc[i] > 0x7e))
			buff[i % 16] = '.';
		else
			buff[i % 16] = pc[i];
		buff[(i % 16) + 1] = '\0';
	}

	// Pad out last line if not exactly 16 characters.
	while ((i % 16) != 0) {
		printf("   ");
		i++;
	}

	// And print the final ASCII bit.
	printf("  %s\n", buff);
}

void RegisterDump(Registers_t *Regs)
{
	/* Dump general regs */
	printf("  EAX: 0x%x, EBX 0x%x, ECX 0x%x, EDX 0x%x\n",
		Regs->Eax, Regs->Ebx, Regs->Ecx, Regs->Edx);

	/* Dump ESI/EBP */
	printf("  ESP 0x%x, EBP 0x%x, Flags 0x%x\n",
		Regs->Esp, Regs->Ebp, Regs->Eflags);

	/* Dump segs */
	printf("  CS 0x%x, DS 0x%x, GS 0x%x, ES 0x%x, FS 0x%x\n",
		Regs->Cs, Regs->Ds, Regs->Gs, Regs->Es, Regs->Fs);
}

/* Common entry for all exceptions */
void ExceptionEntry(Registers_t *regs)
{
	/* We'll need these */
	MCoreThread_t *cThread;
	x86Thread_t *cThreadx86;
	int IssueFixed = 0;
	Cpu_t Cpu;
	//char *instructions = NULL;

	/* Determine Irq */
	if (regs->Irq == 1) 
	{
		/* This is the single-step trap 
		 * exception. */

		/* Dump registers */
		RegisterDump(regs);

		/* Dump stack */
		MemoryDump("Stack dump (64 bytes)", (void*)regs->Esp, 64);

		/* ... no single step for now */
		for (;;);

		/* Otherwise we would reload the flags */
	}
	else if (regs->Irq == 3) 
	{
		/* This is the breakpoint exception */
	}
	else if (regs->Irq == 7)
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

		/* FIRST OF ALL, we check like, SPECIAL addresses */
		if (UnmappedAddr == MEMORY_LOCATION_SIGNAL_RET)
		{
			/* Variables */
			MCoreSignal_t *Signal = NULL;
			MCoreAsh_t *Ash = NULL;

			/* Oh oh, someone has done the dirty signal */
			Cpu = ApicGetCpu();
			cThread = ThreadingGetCurrentThread(Cpu);
			Ash = PhoenixGetAsh(cThread->AshId);

			/* Now.. get active signal */
			Signal = Ash->ActiveSignal;

			/* Restore context */
			//??? neccessary?? 

			/* Cleanup signal */
			Ash->ActiveSignal = NULL;
			kfree(Signal);

			/* Continue into next signal? */
			SignalHandle(cThread->Id);

			/* If we reach here, no more signals, 
			 * and we should just enter the actual thread */
			if (cThread->Flags & (THREADING_USERMODE | THREADING_DRIVERMODE))
				enter_thread(((x86Thread_t*)cThread->ThreadData)->UserContext);
			else
				enter_thread(((x86Thread_t*)cThread->ThreadData)->Context);

			/* We can never reach here tho... */
			kernel_panic("REACHED BEYOND enter_thread AFTER SIGNAL");
		}

		/* Driver Address? */
		if (UnmappedAddr >= MEMORY_LOCATION_IOSPACE
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
				MmVirtualMap(NULL, MmPhysicalAllocateBlock(MEMORY_MASK_DEFAULT, 1), (UnmappedAddr & PAGE_MASK), 0);

				/* Issue is fixed */
				IssueFixed = 1;
			}
		}

		/* User heap address? */
		else if (UnmappedAddr >= MEMORY_LOCATION_USER_HEAP
			&& UnmappedAddr < MEMORY_LOCATION_USER_GUARD)
		{
			/* Get heap */
			MCoreAsh_t *Ash = PhoenixGetAsh(PHOENIX_CURRENT);

			/* Sanity */
			if (Ash != NULL)
			{
				/* Yes, validate it in the heap */
				if (!BitmapValidateAddress(Ash->Heap, UnmappedAddr))
				{
					/* Map in in */
					MmVirtualMap(NULL, MmPhysicalAllocateBlock(MEMORY_MASK_DEFAULT, 1),
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
			MmVirtualMap(NULL, MmPhysicalAllocateBlock(MEMORY_MASK_DEFAULT, 1), (UnmappedAddr & PAGE_MASK), PAGE_USER);

			/* Issue is fixed */
			IssueFixed = 1;
		}
		
		/* Sanity */
		if (IssueFixed == 0)
		{
			/* Variables for lookup */
			Addr_t Base = 0;
			char *Name = NULL;

			/* Get cpu */
			Cpu = ApicGetCpu();

			/* Enable log */
			LogRedirect(LogConsole);

			/* Odd */
			printf("CR2 Address: 0x%x\n", UnmappedAddr);

			/* We could lookup */
			LookupFault(regs->Eip, &Base, &Name);

			/* Sanitize */
			if (Name != NULL && Base != 0) {
				Addr_t Diff = regs->Eip - Base;
				printf("Fauly Address: 0x%x (%s)\n", Diff, Name);
			}
			else {
				printf("Faulty Address: 0x%x\n", regs->Eip);
				printf("Stack ptr: 0x%x\n", regs->Esp);
			}

			/* Log Thread Information */
			LogFatal("SYST", "Thread %s - %u (Core %u)!",
				ThreadingGetCurrentThread(Cpu)->Name,
				ThreadingGetCurrentThreadId(), Cpu);
			StackTrace(6);
			MemoryDump("Stack Contents (last 128 bytes)", (void*)regs->Esp, 128);

			for (;;);
		}
	}
	if (IssueFixed == 0)
	{
		/* Get cpu */
		Cpu = ApicGetCpu();

		/* Enable log */
		LogRedirect(LogConsole);

		printf("Exception Handler! Irq %u, Error Code: %u, Faulty Address: 0x%x\n",
			regs->Irq, regs->ErrorCode, regs->Eip);
		RegisterDump(regs);
		LogFatal("SYST", "Thread %s - %u (Core %u)!",
			ThreadingGetCurrentThread(Cpu)->Name,
			ThreadingGetCurrentThreadId(), Cpu);
		StackTrace(6);
		MemoryDump("Stack Contents (last 128 bytes)", (void*)regs->Esp, 128);

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
	StackTrace(8);

	/* Log Thread Information */
	LogFatal("SYST", "Thread %s - %u (Core %u)!", 
		ThreadingGetCurrentThread(CurrCpu)->Name, 
		ThreadingGetCurrentThreadId(), CurrCpu);
	ThreadingDebugPrint();

	/* Gooodbye */
	InterruptDisable();
	for (;;);
}
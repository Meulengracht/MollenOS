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
 * MollenOS Debugging Interface
 * - Contains the shared kernel debugging interface and tools
 *   available for tracing and debugging
 */
#define __MODULE		"DBGI"
//#define __TRACE

/* Includes
 * - System */
#include <system/addresspace.h>
#include <system/iospace.h>
#include <system/utils.h>
#include <process/phoenix.h>
#include <debug.h>
#include <heap.h>

/* Includes
 * - Library */
#include <stdio.h>
#include <stddef.h>

/* DebugSingleStep
 * Handles the SingleStep trap on a higher level 
 * and the actual interrupt/exception should be propegated
 * to this event handler */
OsStatus_t
DebugSingleStep(
	_In_ Context_t *Context)
{
	// Variables

	// Trace
	TRACE("DebugSingleStep(IP 0x%x)", Context->Eip);

	_CRT_UNUSED(Context);

	// Done
	return OsSuccess;
}

/* DebugBreakpoint
 * Handles the Breakpoint trap on a higher level 
 * and the actual interrupt/exception should be propegated
 * to this event handler */
OsStatus_t
DebugBreakpoint(
	_In_ Context_t *Context)
{
	// Variables

	// Trace
	TRACE("DebugBreakpoint(IP 0x%x)", Context->Eip);

	_CRT_UNUSED(Context);

	// Done
	return OsSuccess;
}

/* DebugPageFault
 * Handles page-fault and either validates or invalidates
 * that the address is valid. In case of valid address it automatically
 * maps in the page and returns OsSuccess */
OsStatus_t
DebugPageFault(
	_In_ Context_t *Context,
	_In_ uintptr_t Address)
{
	// Trace
	TRACE("DebugPageFault(IP 0x%x, Address 0x%x)",
		Context->Eip, Address);
	_CRT_UNUSED(Context);

	// Check the given address in special regions of memory
	// Case 1 - Userspace
	if (Address >= MEMORY_LOCATION_RING3_IOSPACE
		&& Address < MEMORY_LOCATION_RING3_IOSPACE_END) {
		uintptr_t Physical = IoSpaceValidate(Address);
		if (Physical != 0) {
			// Try to map it in and return the result
			return AddressSpaceMapFixed(AddressSpaceGetCurrent(),
				(Physical & PAGE_MASK), (Address & PAGE_MASK), PAGE_SIZE,
				AS_FLAG_NOCACHE | AS_FLAG_APPLICATION);
		}
	}

	// Case 2 - Kernel Heap
	else if (Address >= MEMORY_LOCATION_HEAP
		&& Address < MEMORY_LOCATION_HEAP_END) {
		if (!HeapValidateAddress(HeapGetKernel(), Address)) {
			// Try to map it in and return the result
			return AddressSpaceMap(AddressSpaceGetCurrent(),
				(Address & PAGE_MASK), PAGE_SIZE, __MASK, 0, NULL);
		}
	}

	// Case 3 - User Heap
	else if (Address >= MEMORY_LOCATION_RING3_HEAP
		&& Address < MEMORY_LOCATION_RING3_SHM) {
		MCoreAsh_t *Ash = PhoenixGetCurrentAsh();
		if (Ash != NULL) {
			if (BlockBitmapValidateState(Ash->Heap, Address, 1) == OsSuccess) {
				// Try to map it in and return the result
				return AddressSpaceMap(AddressSpaceGetCurrent(),
					(Address & PAGE_MASK), PAGE_SIZE, __MASK,
					AS_FLAG_APPLICATION, NULL);
			}
		}
	}

	// Case 4 - User Per-Thread Storage (Stack + TLS)
    else if (Address >= MEMORY_LOCATION_RING3_THREAD_START
        && Address <= MEMORY_LOCATION_RING3_THREAD_END) {
		// Try to map it in and return the result
		return AddressSpaceMap(AddressSpaceGetCurrent(),
			(Address & PAGE_MASK), PAGE_SIZE, __MASK,
			AS_FLAG_APPLICATION, NULL);
	}

	// If we reach here, it was invalid
	return OsError;
}

/* DebugPanic
 * Kernel panic function - Call this to enter panic mode
 * and disrupt normal functioning. This function does not
 * return again */
OsStatus_t
DebugPanic(
	_In_ int FatalityScope,
	_In_ __CONST char *Module,
	_In_ __CONST char *Message, ...)
{
	// Variables
	char MessageBuffer[256];
	va_list Arguments;
	UUId_t Cpu;

	// Trace
	TRACE("DebugPanic(Scope %i)", FatalityScope);

	// Lookup some variables
	Cpu = CpuGetCurrentId();

	// Redirect log
	LogRedirect(LogConsole);

	// Format the debug information
	va_start(Arguments, Message);
	vsprintf(&MessageBuffer[0], Message, Arguments);
	va_end(Arguments);

	// Debug print it
	LogFatal(Module, &MessageBuffer[0]);

	// Stack trace
	DebugStackTrace(8);

	// Log cpu and threads
	LogFatal(Module, "Thread %s - %u (Core %u)!",
		ThreadingGetCurrentThread(Cpu)->Name, 
		ThreadingGetCurrentThreadId(), Cpu);
	ThreadingDebugPrint();

	// Handle based on the scope of the fatality
	if (FatalityScope == FATAL_SCOPE_KERNEL) {
		CpuHalt();
	}
	else if (FatalityScope == FATAL_SCOPE_PROCESS) {
		// Dismiss process
	}
	else if (FatalityScope == FATAL_SCOPE_THREAD) {
		// Dismiss thread
	}
	else {
		WARNING("Encounted an unkown fatality scope %i", FatalityScope);
	}

	// Never reached
	return OsSuccess;
}

/* DebugGetModuleByAddress
 * Retrieves the module (Executable) at the given address */
OsStatus_t
DebugGetModuleByAddress(
	_In_ uintptr_t Address,
	_Out_ uintptr_t *Base,
	_Out_ char **Name)
{
	// Validate that the address is within userspace
	if (Address >= MEMORY_LOCATION_RING3_CODE
		&& Address < MEMORY_LOCATION_RING3_HEAP) {
		MCoreAsh_t *Ash = PhoenixGetCurrentAsh();

		// Sanitize whether or not a process was running
		if (Ash != NULL && Ash->Executable != NULL) {
			char *PmName = (char*)MStringRaw(Ash->Executable->Name);
			uintptr_t PmBase = Ash->Executable->VirtualAddress;

			// Iterate libraries to find the sinner
			// We only want one 
			if (Ash->Executable->LoadedLibraries != NULL) {
				foreach(lNode, Ash->Executable->LoadedLibraries) {
					MCorePeFile_t *Lib = (MCorePeFile_t*)lNode->Data;
					if (Address >= Lib->VirtualAddress
						&& Lib->VirtualAddress > PmBase) {
						PmName = (char*)MStringRaw(Lib->Name);
						PmBase = Lib->VirtualAddress;
					}
				}
			}

			// Update out's
			*Base = PmBase;
			*Name = PmName;
			return OsSuccess;
		}
	}

	// Was not present
	*Base = 0;
	*Name = NULL;
	return OsError;
}

/* DebugStackTrace
 * Performs a verbose stack trace in the current context 
 * Goes back a maximum of <MaxFrames> in the stack */
OsStatus_t
DebugStackTrace(
	_In_ size_t MaxFrames)
{
	// Derive stack pointer from the argument
	uintptr_t *StackPtr = (uintptr_t*)&MaxFrames;
	size_t Itr = MaxFrames;

	// Iterate the stack frames by retrieving EBP
	while (Itr != 0 && StackPtr != NULL) {
		uintptr_t Ip = StackPtr[2];
		uintptr_t Base = 0;
		char *Name = NULL;

		// Santize IP
		// It cannot be null and it must be a valid address
		if (Ip == 0
			|| AddressSpaceGetMap(AddressSpaceGetCurrent(), Ip) == 0) {
			break;
		}

		// Lookup module if the address is within userspace
		if (DebugGetModuleByAddress(Ip, &Base, &Name) == OsSuccess) {
			uintptr_t Diff = Ip - Base;
			WRITELINE("%u - 0x%x (%s)", MaxFrames - Itr, Diff, Name);
		}
		else {
			WRITELINE("%u - 0x%x", MaxFrames - Itr, Ip);
		}

		// Again, sanitize the EBP frame as we may
		// reach invalid values
		if (StackPtr[1] == 0
			|| AddressSpaceGetMap(AddressSpaceGetCurrent(), StackPtr[1]) == 0) {
			break;
		}

		// Retrieve argument pointer
		//uint32_t *ArgPtr = &StackPtr[0];

		// Get next EBP frame
		StackPtr = (uint32_t*)StackPtr[1];
		StackPtr--;

		/* Sanitize */
		if (AddressSpaceGetMap(AddressSpaceGetCurrent(), (VirtualAddress_t)StackPtr) == 0) {
			break;
		}

		// One less frame to unwind
		Itr--;
	}

	// Always succeed
	return OsSuccess;
}

/* DebugMemory 
 * Dumps memory in the form of <data> <string> at the
 * given address and length of memory dump */
OsStatus_t
DebugMemory(
	_In_Opt_ __CONST char *Description,
	_In_ void *Address,
	_In_ size_t Length)
{
	// Variables
	uint8_t Buffer[17];
	uint8_t *pc = (unsigned char*)Address;
	size_t i;

	// Output description if given.
	if (Description != NULL) {
		LogDebug(__MODULE, "%s:", Description);
	}

	if (Length == 0) {
		LogDebug(__MODULE, "ZERO LENGTH");
		return OsError;
	}

	// Process every byte in the data.
	for (i = 0; i < Length; i++) {
		// Multiple of 16 means new line (with line offset).

		if ((i % 16) == 0) {
			// Just don't print ASCII for the zeroth line.
			if (i != 0) {
				LogRaw("  %s\n", Buffer);
			}

			// Output the offset.
			LogRaw("  %04x ", i);
		}

		// Now the hex code for the specific character.
		LogRaw(" %02x", pc[i]);

		// And store a printable ASCII character for later.
		if ((pc[i] < 0x20) || (pc[i] > 0x7e)) {
			Buffer[i % 16] = '.';
		}
		else {
			Buffer[i % 16] = pc[i];
		}
		Buffer[(i % 16) + 1] = '\0';
	}

	// Pad out last line if not exactly 16 characters.
	while ((i % 16) != 0) {
		LogRaw("   ");
		i++;
	}

	// And print the final ASCII bit.
	LogRaw("  %s\n", Buffer);
	return OsSuccess;
}

/* DebugContext 
 * Dumps the contents of the given context for debugging */
OsStatus_t
DebugContext(
	_In_ Context_t *Context)
{
	// Dump general registers
	LogDebug(__MODULE, "EAX: 0x%x, EBX 0x%x, ECX 0x%x, EDX 0x%x",
		Context->Eax, Context->Ebx, Context->Ecx, Context->Edx);

	// Dump stack registers
	LogDebug(__MODULE, "ESP 0x%x (UserESP 0x%x), EBP 0x%x, Flags 0x%x",
        Context->Esp, Context->UserEsp, Context->Ebp, Context->Eflags);
        
    // Dump copy registers
	LogDebug(__MODULE, "ESI 0x%x, EDI 0x%x", Context->Esi, Context->Edi);

	// Dump segments
	LogDebug(__MODULE, "CS 0x%x, DS 0x%x, GS 0x%x, ES 0x%x, FS 0x%x",
		Context->Cs, Context->Ds, Context->Gs, Context->Es, Context->Fs);

	// Dump IRQ information
	LogDebug(__MODULE, "IRQ 0x%x, ErrorCode 0x%x, UserSS 0x%x",
		Context->Irq, Context->ErrorCode, Context->UserSs);

	// Return 
	return OsSuccess;
}

/* Disassembles Memory */
//char *get_instructions_at_mem(uintptr_t address)
//{
//	/* We debug 50 bytes of memory */
//	int n;
//	int num_instructions = 1; /* Debug, normal 50 */
//	char *instructions = (char*)kmalloc(0x1000);
//	uintptr_t pointer = address;
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


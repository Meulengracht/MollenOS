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
 * x86-64 Descriptor Table
 * - Interrupt Descriptor Table
 */

#include <arch.h>
#include <gdt.h>
#include <idt.h>
#include <idt_stubs.h>

extern idt_stub_t IdtStubs[IDT_DESCRIPTOR_COUNT];

IdtObject_t __IdtTableObject;
static IdtEntry_t IdtDescriptors[IDT_DESCRIPTOR_COUNT] = { { 0 } };

static void InterruptInstallDefaultGates(void);

static void
IdtInstallDescriptor(
    _In_ int      IntNum,
    _In_ uint64_t Base,
	_In_ uint16_t Selector,
    _In_ uint8_t  Flags,
    _In_ uint8_t  IstIndex)
{
	IdtDescriptors[IntNum].BaseLow    = (uint16_t)(Base & 0xFFFF);
	IdtDescriptors[IntNum].BaseMiddle = (uint16_t)((Base >> 16) & 0xFFFF);
	IdtDescriptors[IntNum].BaseHigh   = (uint32_t)((Base >> 32) & 0xFFFFFFFF);
	IdtDescriptors[IntNum].Selector   = Selector;
	IdtDescriptors[IntNum].Flags      = Flags;
	IdtDescriptors[IntNum].IstIndex   = IstIndex;
	IdtDescriptors[IntNum].Zero       = 0;
}

void IdtInitialize(void)
{
	__IdtTableObject.Limit = (sizeof(IdtEntry_t) * IDT_DESCRIPTOR_COUNT) - 1;
	__IdtTableObject.Base  = (uint32_t)&IdtDescriptors[0];
	InterruptInstallDefaultGates();

	// Override ALL call gates that need on per-thread base
    // INTERRUPT_SYSCALL, INTERRUPT_LAPIC
	IdtInstallDescriptor(INTERRUPT_SYSCALL, (uintptr_t)syscall_entry, 
		GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_TRAP_GATE32, IDT_IST_INDEX_LEGACY);
	IdtInstallDescriptor(INTERRUPT_LAPIC, (uint64_t)IdtStubs[INTERRUPT_LAPIC], 
        GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32, IDT_IST_INDEX_LEGACY);
	IdtInstall();
}

static int
SelectIdtStack(int IdtEntry)
{
	switch (IdtEntry) {
		case 0:         // Div-By-Zero
		case 14: {      // Page-Fault
			return IDT_IST_INDEX_LEGACY;
		};
		
		case 1: {       // Single-Step
			return IDT_IST_INDEX_SS;
		};
		
		case 2: {       // NMI
			return IDT_IST_INDEX_NMI;
		};
		
		case 3: {		// Breakpoint
			return IDT_IST_INDEX_DBG;
		};
		
		case 4:			// Overflow
		case 5:			// Bound-Range Exceeded
		case 6:			// Invalid Opcode
		case 7:  		// DeviceNotAvailable
		case 9:			// Coprocessor Segment Overrun
		case 10:		// Invalid TSS
		case 11:		// Segment Not Present
		case 12:		// Stack Segment Fault
		case 13:		// General Protection Fault
		case 15:		//
		case 16:		// FPU Exception
		case 17:
		case 19:		// SIMD Floating Point Exception
		case 20:
		case 21:
		case 22:
		case 23:
		case 24:
		case 25:
		case 26:
		case 27:
		case 28:
		case 29:
		case 30:
		case 31: {
			return IDT_IST_INDEX_EXC;
		};
		
		case 8: {		// Double Fault
			return IDT_IST_INDEX_DBF;
		};
		
		case 18: {
			return IDT_IST_INDEX_MCE;
		};
		
		default:
			break;
	}
	return IDT_IST_INDEX_ISR;
}

static void
InterruptInstallDefaultGates(void)
{
	int i;
	
	// Install exception handlers
	for (i = 0; i < 32; i++) {
		int StackIndex = SelectIdtStack(i);
		IdtInstallDescriptor(i, (uint64_t)IdtStubs[i], GDT_KCODE_SEGMENT, 
			IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32, StackIndex);
	}
	
	// Install interrupt routine handlers
	for (; i < IDT_DESCRIPTOR_COUNT; i++) {
		int StackIndex = SelectIdtStack(i);
		IdtInstallDescriptor(i, (uint64_t)IdtStubs[i], GDT_KCODE_SEGMENT,
			IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32, StackIndex);
	}
}

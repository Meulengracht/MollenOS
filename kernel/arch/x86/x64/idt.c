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
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * x86-64 Descriptor Table
 * - Interrupt Descriptor Table
 */

#include <arch/x86/arch.h>
#include <arch/x86/x64/gdt.h>
#include <arch/x86/x64/idt.h>
#include <arch/x86/idt_stubs.h>

extern idt_stub_t IdtStubs[IDT_DESCRIPTOR_COUNT];

IdtObject_t       g_idtTable;
static IdtEntry_t g_idtDescriptors[IDT_DESCRIPTOR_COUNT] = {{0 } };

static void InterruptInstallDefaultGates(void);
static int  SelectIdtStack(int idtIndex);

static void
IdtInstallDescriptor(
    _In_ int      idtIndex,
    _In_ uint64_t handleAddress,
	_In_ uint16_t codeSegment,
    _In_ uint8_t  flags,
    _In_ uint8_t  istIndex)
{
    g_idtDescriptors[idtIndex].BaseLow    = (uint16_t)(handleAddress & 0xFFFF);
    g_idtDescriptors[idtIndex].BaseMiddle = (uint16_t)((handleAddress >> 16) & 0xFFFF);
    g_idtDescriptors[idtIndex].BaseHigh   = (uint32_t)((handleAddress >> 32) & 0xFFFFFFFF);
    g_idtDescriptors[idtIndex].Selector   = codeSegment;
    g_idtDescriptors[idtIndex].Flags      = flags;
    g_idtDescriptors[idtIndex].IstIndex   = istIndex;
    g_idtDescriptors[idtIndex].Zero       = 0;
}

void IdtInitialize(void)
{
    g_idtTable.Limit = (sizeof(IdtEntry_t) * IDT_DESCRIPTOR_COUNT) - 1;
    g_idtTable.Base  = (uint64_t)&g_idtDescriptors[0];
	InterruptInstallDefaultGates();

	// Override the system call entry, as also described in _irq.s when we are in 64 bit
	// we want to still disable interrupts on entry for system calls, due to the nature of
	// swapgs instruction. If we are interrupted before the swapgs instruction, we end up
	// double swapping
	IdtInstallDescriptor(INTERRUPT_SYSCALL, (uintptr_t)syscall_entry, 
		GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32,
		SelectIdtStack(INTERRUPT_SYSCALL));
	IdtInstall();
}

static int SelectIdtStack(int idtIndex)
{
    // Handle exceptions that are suscibtle to stack issues
	switch (idtIndex) {
	    case 1:  { return IDT_IST_INDEX_DB;  }
	    case 2:  { return IDT_IST_INDEX_NMI; }
        case 8:  { return IDT_IST_INDEX_DBF; };
        case 14: { return IDT_IST_INDEX_PF;  };
        case 18: { return IDT_IST_INDEX_MCE; };

		default:
			break;
	}

	// Default to legacy stack switch mechanism
	return IDT_IST_INDEX_LEGACY;
}

static void
InterruptInstallDefaultGates(void)
{
	int i;
	
	// Install exception handlers
	for (i = 0; i < 32; i++) {
		int stackIndex = SelectIdtStack(i);
		IdtInstallDescriptor(i, (uint64_t)IdtStubs[i], GDT_KCODE_SEGMENT, 
			IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32, stackIndex);
	}
	
	// Install interrupt routine handlers
	for (; i < IDT_DESCRIPTOR_COUNT; i++) {
		int stackIndex = SelectIdtStack(i);
		IdtInstallDescriptor(i, (uint64_t)IdtStubs[i], GDT_KCODE_SEGMENT,
			IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32, stackIndex);
	}
}

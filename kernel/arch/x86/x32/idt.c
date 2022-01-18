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
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * x86-32 Descriptor Table
 * - Interrupt Descriptor Table
 */

#include <arch/x86/arch.h>
#include <arch/x86/x32/gdt.h>
#include <arch/x86/x32/idt.h>
#include <arch/x86/idt_stubs.h>

extern idt_stub_t IdtStubs[IDT_DESCRIPTOR_COUNT];

IdtObject_t Idtptr;
static IdtEntry_t IdtDescriptors[IDT_DESCRIPTOR_COUNT] = { { 0 } };

static void InterruptInstallDefaultGates(void);

static void
IdtInstallDescriptor(
    _In_ int       IntNum,
    _In_ uintptr_t Base,
    _In_ uint16_t  Selector,
    _In_ uint8_t   Flags)
{
    IdtDescriptors[IntNum].BaseLow  = (Base & 0xFFFF);
    IdtDescriptors[IntNum].BaseHigh = ((Base >> 16) & 0xFFFF);
    IdtDescriptors[IntNum].Selector = Selector;
    IdtDescriptors[IntNum].Zero     = 0;
    IdtDescriptors[IntNum].Flags    = Flags;
}

void IdtInitialize(void)
{
    Idtptr.Limit = (sizeof(IdtEntry_t) * IDT_DESCRIPTOR_COUNT) - 1;
    Idtptr.Base  = (uint32_t)&IdtDescriptors[0];
    InterruptInstallDefaultGates();
    
    IdtInstallDescriptor(INTERRUPT_SYSCALL, (uintptr_t)syscall_entry, 
        GDT_KCODE_SEGMENT, IDT_RING3 | IDT_PRESENT | IDT_TRAP_GATE32);
    IdtInstall();
}

static void
InterruptInstallDefaultGates(void)
{
	int i;
	
	// Install exception handlers
	for (i = 0; i < 32; i++) {
		IdtInstallDescriptor(i, (uint64_t)IdtStubs[i], GDT_KCODE_SEGMENT, 
			IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
	}
	
	// Install interrupt routine handlers
	for (; i < IDT_DESCRIPTOR_COUNT; i++) {
		IdtInstallDescriptor(i, (uint64_t)IdtStubs[i], GDT_KCODE_SEGMENT,
			IDT_RING3 | IDT_PRESENT | IDT_INTERRUPT_GATE32);
	}
}

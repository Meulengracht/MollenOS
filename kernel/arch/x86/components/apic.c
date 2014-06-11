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
* MollenOS x86-32 Advanced Programmable Interrupt Controller Driver
*/

#include <arch.h>
#include <acpi.h>
#include <lapic.h>
#include <cpu.h>

#include <stdio.h>

/* Globals */
extern volatile addr_t local_apic_addr;

void apic_init(void)
{	
	/* Disable the PIC */
	outb(0x21, 0xFF);
	outb(0xA1, 0xFF);

	/* Force NMI and legacy through apic in IMCR */
	outb(0x22, 0x70);
	outb(0x23, 0x1);

	/* Setup Local Apic */
	lapic_init();

	/* Now! Setup I/O Apics */
}

/* Enable/Config LAPIC */
void lapic_init(void)
{
	/* Set destination format register to flat model */
	apic_write_local(LAPIC_DEST_FORMAT, apic_read_local(LAPIC_DEST_FORMAT) & 0xF0000000);

	/* Set bits 31-24 for all cores to be in Group 1 in logical destination register */
	apic_write_local(LAPIC_LOGICAL_DEST, 0xFF000000);

	/* Reset register states */
	apic_write_local(LAPIC_ERROR_REGISTER, 0x10000);
	apic_write_local(LAPIC_LINT0_REGISTER, 0x10000);
	apic_write_local(LAPIC_LINT1_REGISTER, 0x10000);
	apic_write_local(LAPIC_PERF_MONITOR, 0x10000);
	apic_write_local(LAPIC_THERMAL_SENSOR, 0x10000);
	apic_write_local(LAPIC_TIMER_VECTOR, 0x10000);

	/* Enable local apic */
	/* Install Spurious vector */
	apic_write_local(LAPIC_SPURIOUS_REG, 0x100 | INTERRUPT_SPURIOUS);	// Set bit 8 in SVR.
}

/* Read / Write to local apic registers */
uint32_t apic_read_local(uint32_t reg)
{
	return (uint32_t)(*(volatile addr_t*)(local_apic_addr + reg));
}

void apic_write_local(uint32_t reg, uint32_t value)
{
	(*(volatile addr_t*)(local_apic_addr + reg)) = value;
}

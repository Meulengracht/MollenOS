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
 * MollenOS x86 Advanced Programmable Interrupt Controller Driver
 * - IO Helpers, primarily just for read/write
 */

#include <assert.h>
#include <arch/x86/apic.h>
#include <acpi.h>

__EXTERN uintptr_t g_localApicBaseAddress;

/* Reads from the local apic registers 
 * Reads and writes from and to the local apic
 * registers must always be 32 bit */
uint32_t ApicReadLocal(size_t Register) {
	assert(g_localApicBaseAddress != 0);
	return (uint32_t)(*(volatile uint32_t*)(g_localApicBaseAddress + Register));
}

/* Write to the local apic registers 
 * Reads and writes from and to the local apic registers must always be 32 bit */
void ApicWriteLocal(size_t Register, uint32_t Value) {
	assert(g_localApicBaseAddress != 0);

	/* Write the value, then re-read it to 
	 * ensure memory synchronization */
	(*(volatile uint32_t*)(g_localApicBaseAddress + Register)) = Value;
	Value = (*(volatile uint32_t*)(g_localApicBaseAddress + Register));
}

/* Set the io-apic register selctor
 * Reads and writes from and to the io apic
 * registers must always be 32 bit */
void ApicSetIoRegister(SystemInterruptController_t *IoApic, uint32_t Register) {
	(*(volatile uint32_t*)(IoApic->MemoryAddress)) = Register;
}

/* Read from io-apic registers
 * Reads and writes from and to the io apic registers must always be 32 bit */
uint32_t ApicIoRead(SystemInterruptController_t *IoApic, uint32_t Register) {
	assert(IoApic != NULL);

	// Select the given register and read the register
	ApicSetIoRegister(IoApic, Register);
	return *((volatile uint32_t*)(IoApic->MemoryAddress + 0x10));
}

/* Write to the io-apic registers
 * Reads and writes from and to the io apic registers must always be 32 bit */
void ApicIoWrite(SystemInterruptController_t *IoApic, uint32_t Register, uint32_t Data) {
	assert(IoApic != NULL);

	// Write the value, then re-read it to ensure memory synchronization
	ApicSetIoRegister(IoApic, Register);
	*((volatile uint32_t*)(IoApic->MemoryAddress + 0x10)) = Data;
	Data = (*(volatile uint32_t*)(IoApic->MemoryAddress + 0x10));
}

/* Writes interrupt data to the io-apic interrupt register. It writes the data to
 * the given Pin (io-apic entry) offset. */
void ApicWriteIoEntry(SystemInterruptController_t *IoApic, int Pin, uint64_t Data)
{
	// Union this for easier 
	// memory access becuase we do 32 bit accesses
	union {
		struct {
			uint32_t Lo;
			uint32_t Hi;
		} Parts;
		uint64_t Full;
	} Value;

	/* Get the correct IO APIC address */
	uint32_t Register = 0x10 + (2 * Pin);
	Value.Full = Data;

	/* Write the dwords seperately as 
	 * the writes has to be 32 bit */
	ApicIoWrite(IoApic, Register + 1, Value.Parts.Hi);
	ApicIoWrite(IoApic, Register, Value.Parts.Lo);
}

/* Reads interrupt data from the io-apic
 * interrupt register. It reads the data from
 * the given Pin (io-apic entry) offset. */
uint64_t ApicReadIoEntry(SystemInterruptController_t *IoApic, int Pin)
{
	/* Union this for easier 
	 * memory access becuase we do 32 bit accesses */
	union {
		struct {
			uint32_t Lo;
			uint32_t Hi;
		} Parts;
		uint64_t Full;
	} Value;

	/* Get the correct IO APIC address */
	uint32_t Register = 0x10 + (2 * Pin);

	/* Read the dwords seperately as 
	 * the reads has to be 32 bit */
	Value.Parts.Hi = ApicIoRead(IoApic, Register + 1);
	Value.Parts.Lo = ApicIoRead(IoApic, Register);

	/* Return the full 64 bit value */
	return Value.Full;
}

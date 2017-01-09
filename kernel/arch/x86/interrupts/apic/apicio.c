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
 * MollenOS x86 Advanced Programmable Interrupt Controller Driver
 * - IO Helpers, primarily just for read/write
 */

/* Includes 
 * - System */
#include <Apic.h>
#include <acpi.h>

/* Includes 
 * - C-Library */
#include <assert.h>
#include <stddef.h>

/* Externs we need for functions here, all
 * the i/o functions needs the base address */
__CRT_EXTERN Addr_t GlbLocalApicBase;

/* Reads from the local apic registers 
 * Reads and writes from and to the local apic
 * registers must always be 32 bit */
uint32_t ApicReadLocal(size_t Register)
{
	/* Sanitize the local base
	 * to protect against bad setup sequence */
	assert(GlbLocalApicBase != 0);
	return (uint32_t)(*(volatile uint32_t*)(GlbLocalApicBase + Register));
}

/* Write to the local apic registers 
 * Reads and writes from and to the local apic
 * registers must always be 32 bit */
void ApicWriteLocal(size_t Register, uint32_t Value)
{
	/* Sanitize the local base
	 * to protect against bad setup sequence */
	assert(GlbLocalApicBase != 0);

	/* Write the value, then re-read it to 
	 * ensure memory synchronization */
	(*(volatile uint32_t*)(GlbLocalApicBase + Register)) = Value;
	Value = (*(volatile uint32_t*)(GlbLocalApicBase + Register));
}

/* Set the io-apic register selctor
 * Reads and writes from and to the io apic
 * registers must always be 32 bit */
void ApicSetIoRegister(IoApic_t *IoApic, uint32_t Register)
{
	/* Write the value, then do a memory barrier
	 * to ensure transaction has been made */
	(*(volatile uint32_t*)(IoApic->BaseAddress)) = Register;
	MemoryBarrier();
}

/* Read from io-apic registers
 * Reads and writes from and to the io apic
 * registers must always be 32 bit */
uint32_t ApicIoRead(IoApic_t *IoApic, uint32_t Register)
{
	/* Sanitize the io-apic structure 
	 * to protect against bad code */
	assert(IoApic != NULL);

	/* Select the given register 
	 * and read the register */
	ApicSetIoRegister(IoApic, Register);
	return *((volatile uint32_t*)(IoApic->BaseAddress + 0x10));
}

/* Write to the io-apic registers
 * Reads and writes from and to the io apic
 * registers must always be 32 bit */
void ApicIoWrite(IoApic_t *IoApic, uint32_t Register, uint32_t Data)
{
	/* Sanitize the io-apic structure 
	 * to protect against bad code */
	assert(IoApic != NULL);

	/* Select the given register 
	 * and write the register */
	ApicSetIoRegister(IoApic, Register);
	*(volatile Addr_t*)(IoApic->BaseAddress + 0x10) = Data;
	MemoryBarrier();
}

/* Writes interrupt data to the io-apic
 * interrupt register. It writes the data to
 * the given Pin (io-apic entry) offset. */
void ApicWriteIoEntry(IoApic_t *IoApic, uint32_t Pin, uint64_t Data)
{
	/* Get the correct IO APIC address */
	uint32_t Register = 0x10 + (2 * Pin);

	/* Write the dwords seperately as 
	 * the writes has to be 32 bit */
	ApicIoWrite(IoApic, Register + 1, ACPI_HIDWORD(Data));
	ApicIoWrite(IoApic, Register, ACPI_LODWORD(Data));
}

/* Reads interrupt data from the io-apic
 * interrupt register. It reads the data from
 * the given Pin (io-apic entry) offset. */
uint64_t ApicReadIoEntry(IoApic_t *IoApic, uint32_t Pin)
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

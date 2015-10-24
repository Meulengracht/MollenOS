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

/* Includes */
#include <Apic.h>
#include <acpi.h>
#include <assert.h>
#include <stddef.h>

/* Externs */
extern volatile Addr_t GlbLocalApicAddress;

/* Read / Write to local apic registers */
uint32_t ApicReadLocal(uint32_t Register)
{
	/* Simply just read */
	return (uint32_t)(*(volatile Addr_t*)(GlbLocalApicAddress + Register));
}

void ApicWriteLocal(uint32_t Register, uint32_t Value)
{
	/* Write */
	(*(volatile Addr_t*)(GlbLocalApicAddress + Register)) = Value;

	/* Re-read new value to sync */
	Value = (*(volatile Addr_t*)(GlbLocalApicAddress + Register));
}

/* Read / Write to io apic registers */
void ApicSetIoRegister(IoApic_t *IoApic, uint32_t Register)
{
	/* Set register */
	(*(volatile Addr_t*)(IoApic->BaseAddress)) = (uint32_t)Register;
}

/* Read from Io Apic register */
uint32_t ApicIoRead(IoApic_t *IoApic, uint32_t Register)
{
	/* Sanity */
	assert(IoApic != NULL);

	/* Select register */
	ApicSetIoRegister(IoApic, Register);

	/* Read */
	return *(volatile Addr_t*)(IoApic->BaseAddress + 0x10);
}

/* Write to Io Apic register */
void ApicIoWrite(IoApic_t *IoApic, uint32_t Register, uint32_t Data)
{
	/* Sanity */
	assert(IoApic != NULL);

	/* Select register */
	ApicSetIoRegister(IoApic, Register);

	/* Write */
	*(volatile Addr_t*)(IoApic->BaseAddress + 0x10) = Data;
}

/* Insert a 64 bit entry into the Io Apic Intr */
void ApicWriteIoEntry(IoApic_t *IoApic, uint32_t Pin, uint64_t Data)
{
	/* Get the correct IO APIC address */
	uint32_t IoReg = 0x10 + (2 * Pin);

	/* Sanity */
	assert(IoApic != NULL);

	/* Select register */
	ApicSetIoRegister(IoApic, IoReg + 1);

	/* Write High data */
	(*(volatile Addr_t*)(IoApic->BaseAddress + 0x10)) = ACPI_HIDWORD(Data);

	/* Reselect*/
	ApicSetIoRegister(IoApic, IoReg);

	/* Write lower */
	(*(volatile Addr_t*)(IoApic->BaseAddress + 0x10)) = ACPI_LODWORD(Data);
}

/* Retrieve a 64 bit entry from the Io Apic Intr */
uint64_t ApicReadIoEntry(IoApic_t *IoApic, uint32_t Register)
{
	uint32_t lo, hi;
	uint64_t val;

	/* Sanity */
	assert(IoApic != NULL);

	/* Select register */
	ApicSetIoRegister(IoApic, Register + 1);

	/* Read high word */
	hi = *(volatile Addr_t*)(IoApic->BaseAddress + 0x10);

	/* Select next register */
	ApicSetIoRegister(IoApic, Register);

	/* Read low word */
	lo = *(volatile Addr_t*)(IoApic->BaseAddress + 0x10);

	/* Build */
	val = hi;
	val <<= 32;
	val |= lo;
	return val;
}

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
 * MollenOS MCore - ACPICA Support Layer (IO Functions)
 *  - Missing implementations are todo
 */
#define __MODULE "ACPI"
#define __TRACE

/* Includes
 * - (OS) System */
#include "../../../arch/x86/pci.h"
#include <system/addresspace.h>
#include <system/utils.h>
#include <interrupts.h>
#include <threading.h>
#include <scheduler.h>
#include <debug.h>
#include <arch.h>

/* Includes
 * - (ACPI) System */
#include <acpi.h>
#include <accommon.h>

/* Definitions
 * - Component Setup */
#define _COMPONENT ACPI_OS_SERVICES
ACPI_MODULE_NAME("oslayer_io")

/******************************************************************************
 *
 * FUNCTION:    AcpiOsReadPort
 *
 * PARAMETERS:  Address             - Address of I/O port/register to read
 *              Value               - Where value is placed
 *              Width               - Number of bits
 *
 * RETURN:      Value read from port
 *
 * DESCRIPTION: Read data from an I/O port or register
 *
 *****************************************************************************/
ACPI_STATUS
AcpiOsReadPort (
    ACPI_IO_ADDRESS         Address,
    UINT32                  *Value,
    UINT32                  Width)
{
    ACPI_FUNCTION_NAME(OsReadPort);

	switch (Width) {
        case 8:
            *Value = inb((uint16_t)Address);
            break;

        case 16:
            *Value = inw((uint16_t)Address);
            break;

        case 32:
            *Value = inl((uint16_t)Address);
            break;

        default:
            ACPI_ERROR((AE_INFO, "Bad width parameter: %X", Width));
            return (AE_BAD_PARAMETER);
	}

	return (AE_OK);
}

/******************************************************************************
 *
 * FUNCTION:    AcpiOsWritePort
 *
 * PARAMETERS:  Address             - Address of I/O port/register to write
 *              Value               - Value to write
 *              Width               - Number of bits
 *
 * RETURN:      None
 *
 * DESCRIPTION: Write data to an I/O port or register
 *
 *****************************************************************************/
ACPI_STATUS
AcpiOsWritePort (
    ACPI_IO_ADDRESS         Address,
    UINT32                  Value,
    UINT32                  Width)
{
    ACPI_FUNCTION_NAME(OsWritePort);
    
    if ((Width == 8) || (Width == 16) || (Width == 32)) {
		switch (Width) {
            case 8:
                outb((uint16_t)Address, (uint8_t)Value);
                break;
            case 16:
                outw((uint16_t)Address, (uint16_t)Value);
                break;
            case 32:
                outl((uint16_t)Address, (uint32_t)Value);
                break;
		}
		return (AE_OK);
	}

	ACPI_ERROR((AE_INFO, "Bad width parameter: %X", Width));
	return (AE_BAD_PARAMETER);
}


/******************************************************************************
 *
 * FUNCTION:    AcpiOsReadMemory
 *
 * PARAMETERS:  Address             - Physical Memory Address to read
 *              Value               - Where value is placed
 *              Width               - Number of bits (8,16,32, or 64)
 *
 * RETURN:      Value read from physical memory address. Always returned
 *              as a 64-bit integer, regardless of the read width.
 *
 * DESCRIPTION: Read data from a physical memory address
 *
 *****************************************************************************/
ACPI_STATUS
AcpiOsReadMemory (
    ACPI_PHYSICAL_ADDRESS   Address,
    UINT64                  *Value,
    UINT32                  Width)
{
    ACPI_FUNCTION_NAME(AcpiOsReadMemory);

    switch (Width) {
        case 8: {
            *Value = 0;
        } break;
        case 16: {
            *Value = 0;
        } break;
        case 32: {
            *Value = 0;
        } break;
        case 64: {
            *Value = 0;
        } break;
        default: {
            return (AE_BAD_PARAMETER);
            break;
        }
    }
    
    FATAL(FATAL_SCOPE_KERNEL, "AcpiOsReadMemory()");
    return AE_NOT_IMPLEMENTED;
}

/******************************************************************************
 *
 * FUNCTION:    AcpiOsWriteMemory
 *
 * PARAMETERS:  Address             - Physical Memory Address to write
 *              Value               - Value to write
 *              Width               - Number of bits (8,16,32, or 64)
 *
 * RETURN:      None
 *
 * DESCRIPTION: Write data to a physical memory address
 *
 *****************************************************************************/
ACPI_STATUS
AcpiOsWriteMemory (
    ACPI_PHYSICAL_ADDRESS   Address,
    UINT64                  Value,
    UINT32                  Width)
{
    ACPI_FUNCTION_NAME(AcpiOsWriteMemory);
    FATAL(FATAL_SCOPE_KERNEL, "AcpiOsWriteMemory()");
    return AE_NOT_IMPLEMENTED;
}

/******************************************************************************
 *
 * FUNCTION:    AcpiOsReadPciConfiguration
 *
 * PARAMETERS:  PciId               - Seg/Bus/Dev
 *              Register            - Device Register
 *              Value               - Buffer where value is placed
 *              Width               - Number of bits
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Read data from PCI configuration space
 *
 *****************************************************************************/
ACPI_STATUS
AcpiOsReadPciConfiguration (
    ACPI_PCI_ID             *PciId,
    UINT32                  Reg,
    UINT64                  *Value,
    UINT32                  Width)
{
    ACPI_FUNCTION_NAME(AcpiOsReadPciConfiguration);
    switch (Width) {
		case 8: {
			*Value = (UINT64)PciRead8(PciId->Bus, PciId->Device, PciId->Function, Reg);
		} break;

		case 16: {
			*Value = (UINT64)PciRead16(PciId->Bus, PciId->Device, PciId->Function, Reg);
		} break;

		case 32: {
			*Value = (UINT64)PciRead32(PciId->Bus, PciId->Device, PciId->Function, Reg);
		} break;

		default:
			return (AE_ERROR);
	}
	return (AE_OK);
}

/******************************************************************************
 *
 * FUNCTION:    AcpiOsWritePciConfiguration
 *
 * PARAMETERS:  PciId               - Seg/Bus/Dev
 *              Register            - Device Register
 *              Value               - Value to be written
 *              Width               - Number of bits
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Write data to PCI configuration space
 *
 *****************************************************************************/
ACPI_STATUS
AcpiOsWritePciConfiguration (
    ACPI_PCI_ID             *PciId,
    UINT32                  Reg,
    UINT64                  Value,
    UINT32                  Width)
{
    ACPI_FUNCTION_NAME(AcpiOsWritePciConfiguration);
    switch (Width) {
		case 8: {
			PciWrite8(PciId->Bus, PciId->Device, PciId->Function, Reg, (UINT8)Value);
		} break;

		case 16: {
			PciWrite16(PciId->Bus, PciId->Device, PciId->Function, Reg, (UINT16)Value);
		} break;

		case 32: {
			PciWrite32(PciId->Bus, PciId->Device, PciId->Function, Reg, (UINT32)Value);
		} break;

		default:
			return (AE_ERROR);
	}
	return (AE_OK);
}

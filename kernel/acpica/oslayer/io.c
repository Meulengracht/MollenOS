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
#include <system/utils.h>
#include <system/io.h>
#include <interrupts.h>
#include <threading.h>
#include <scheduler.h>
#include <debug.h>

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
    if (IoRead(IO_SOURCE_HARDWARE, Address, DIVUP(Width, 8), Value) != OsSuccess) {
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
    if (IoWrite(IO_SOURCE_HARDWARE, Address, DIVUP(Width, 8), Value) != OsSuccess) {
        ACPI_ERROR((AE_INFO, "Bad width parameter: %X", Width));
        return (AE_BAD_PARAMETER);
    }
    return (AE_OK);
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
    if (IoRead(IO_SOURCE_MEMORY, Address, DIVUP(Width, 8), (size_t*)Value) != OsSuccess) {
        ACPI_ERROR((AE_INFO, "Bad width parameter: %X", Width));
        return (AE_BAD_PARAMETER);
    }
    return (AE_OK);
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
    if (IoWrite(IO_SOURCE_MEMORY, Address, DIVUP(Width, 8), (size_t)Value) != OsSuccess) {
        ACPI_ERROR((AE_INFO, "Bad width parameter: %X", Width));
        return (AE_BAD_PARAMETER);
    }
    return (AE_OK);
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
    if (PciRead(PciId->Bus, PciId->Device, PciId->Function, Reg, DIVUP(Width, 8), (size_t*)Value) != OsSuccess) {
        ACPI_ERROR((AE_INFO, "Bad width parameter: %X", Width));
        return (AE_BAD_PARAMETER);
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
    if (PciWrite(PciId->Bus, PciId->Device, PciId->Function, Reg, DIVUP(Width, 8), (size_t)Value) != OsSuccess) {
        ACPI_ERROR((AE_INFO, "Bad width parameter: %X", Width));
        return (AE_BAD_PARAMETER);
    }
    return (AE_OK);
}

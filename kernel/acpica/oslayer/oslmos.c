/******************************************************************************
*
* Module Name: oslmos - MollenOS OSL
*
*****************************************************************************/

/******************************************************************************
*
* 1. Copyright Notice
*
* Some or all of this work - Copyright (c) 1999 - 2014, Intel Corp.
* All rights reserved.
*
* 2. License
*
* 2.1. This is your license from Intel Corp. under its intellectual property
* rights. You may have additional license terms from the party that provided
* you this software, covering your right to use that party's intellectual
* property rights.
*
* 2.2. Intel grants, free of charge, to any person ("Licensee") obtaining a
* copy of the source code appearing in this file ("Covered Code") an
* irrevocable, perpetual, worldwide license under Intel's copyrights in the
* base code distributed originally by Intel ("Original Intel Code") to copy,
* make derivatives, distribute, use and display any portion of the Covered
* Code in any form, with the right to sublicense such rights; and
*
* 2.3. Intel grants Licensee a non-exclusive and non-transferable patent
* license (with the right to sublicense), under only those claims of Intel
* patents that are infringed by the Original Intel Code, to make, use, sell,
* offer to sell, and import the Covered Code and derivative works thereof
* solely to the minimum extent necessary to exercise the above copyright
* license, and in no event shall the patent license extend to any additions
* to or modifications of the Original Intel Code. No other license or right
* is granted directly or by implication, estoppel or otherwise;
*
* The above copyright and patent license is granted only if the following
* conditions are met:
*
* 3. Conditions
*
* 3.1. Redistribution of Source with Rights to Further Distribute Source.
* Redistribution of source code of any substantial portion of the Covered
* Code or modification with rights to further distribute source must include
* the above Copyright Notice, the above License, this list of Conditions,
* and the following Disclaimer and Export Compliance provision. In addition,
* Licensee must cause all Covered Code to which Licensee contributes to
* contain a file documenting the changes Licensee made to create that Covered
* Code and the date of any change. Licensee must include in that file the
* documentation of any changes made by any predecessor Licensee. Licensee
* must include a prominent statement that the modification is derived,
* directly or indirectly, from Original Intel Code.
*
* 3.2. Redistribution of Source with no Rights to Further Distribute Source.
* Redistribution of source code of any substantial portion of the Covered
* Code or modification without rights to further distribute source must
* include the following Disclaimer and Export Compliance provision in the
* documentation and/or other materials provided with distribution. In
* addition, Licensee may not authorize further sublicense of source of any
* portion of the Covered Code, and must include terms to the effect that the
* license from Licensee to its licensee is limited to the intellectual
* property embodied in the software Licensee provides to its licensee, and
* not to intellectual property embodied in modifications its licensee may
* make.
*
* 3.3. Redistribution of Executable. Redistribution in executable form of any
* substantial portion of the Covered Code or modification must reproduce the
* above Copyright Notice, and the following Disclaimer and Export Compliance
* provision in the documentation and/or other materials provided with the
* distribution.
*
* 3.4. Intel retains all right, title, and interest in and to the Original
* Intel Code.
*
* 3.5. Neither the name Intel nor any other trademark owned or controlled by
* Intel shall be used in advertising or otherwise to promote the sale, use or
* other dealings in products derived from or relating to the Covered Code
* without prior written authorization from Intel.
*
* 4. Disclaimer and Export Compliance
*
* 4.1. INTEL MAKES NO WARRANTY OF ANY KIND REGARDING ANY SOFTWARE PROVIDED
* HERE. ANY SOFTWARE ORIGINATING FROM INTEL OR DERIVED FROM INTEL SOFTWARE
* IS PROVIDED "AS IS," AND INTEL WILL NOT PROVIDE ANY SUPPORT, ASSISTANCE,
* INSTALLATION, TRAINING OR OTHER SERVICES. INTEL WILL NOT PROVIDE ANY
* UPDATES, ENHANCEMENTS OR EXTENSIONS. INTEL SPECIFICALLY DISCLAIMS ANY
* IMPLIED WARRANTIES OF MERCHANTABILITY, NONINFRINGEMENT AND FITNESS FOR A
* PARTICULAR PURPOSE.
*
* 4.2. IN NO EVENT SHALL INTEL HAVE ANY LIABILITY TO LICENSEE, ITS LICENSEES
* OR ANY OTHER THIRD PARTY, FOR ANY LOST PROFITS, LOST DATA, LOSS OF USE OR
* COSTS OF PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES, OR FOR ANY INDIRECT,
* SPECIAL OR CONSEQUENTIAL DAMAGES ARISING OUT OF THIS AGREEMENT, UNDER ANY
* CAUSE OF ACTION OR THEORY OF LIABILITY, AND IRRESPECTIVE OF WHETHER INTEL
* HAS ADVANCE NOTICE OF THE POSSIBILITY OF SUCH DAMAGES. THESE LIMITATIONS
* SHALL APPLY NOTWITHSTANDING THE FAILURE OF THE ESSENTIAL PURPOSE OF ANY
* LIMITED REMEDY.
*
* 4.3. Licensee shall not export, either directly or indirectly, any of this
* software or system incorporating such software without first obtaining any
* required license or other approval from the U. S. Department of Commerce or
* any other agency or department of the United States Government. In the
* event Licensee exports any such software from the United States or
* re-exports any such software from a foreign destination, Licensee shall
* ensure that the distribution and export/re-export of the software is in
* compliance with all laws, regulations, orders, or other restrictions of the
* U.S. Export Administration Regulations. Licensee agrees that neither it nor
* any of its subsidiaries will export/re-export any technical data, process,
* software, or service, directly or indirectly, to any country for which the
* United States government or any agency thereof requires an export license,
* other governmental approval, or letter of assurance, without first obtaining
* such license, approval or letter.
*
*****************************************************************************/

/* Includes
 * - ACPI */
#include "include\acpi.h"
#include "include\accommon.h"

/* warning C4115: named type definition in parentheses (caused by rpcasync.h> */
#pragma warning(disable:4115)   

/* Includes
 * - System */
#include "..\arch\x86\x32\arch.h"
#include "..\arch\x86\pci.h"
#include "..\arch\x86\memory.h"
#include <threading.h>
#include <interrupts.h>
#include <semaphore.h>
#include <timers.h>
#include <heap.h>

/* Includes
 * - Library */
#include <stdio.h>

#define _COMPONENT          ACPI_OS_SERVICES
ACPI_MODULE_NAME("oslmos")

/* Globals */
volatile void *Acpi_RedirectionTarget = NULL;
UUId_t GlbAcpiInterruptId = UUID_INVALID;

/******************************************************************************
*
* FUNCTION:    AcpiOsInitialize
*
* PARAMETERS:  None
*
* RETURN:      Status
*
* DESCRIPTION: Init this OSL
*
*****************************************************************************/

ACPI_STATUS AcpiOsInitialize(void)
{
	/* Initialize the globals */
	Acpi_RedirectionTarget = NULL;
	GlbAcpiInterruptId = UUID_INVALID;
	return (AE_OK);
}


/******************************************************************************
*
* FUNCTION:    AcpiOsTerminate
*
* PARAMETERS:  None
*
* RETURN:      Status
*
* DESCRIPTION: Nothing to do for MollenOS
*
*****************************************************************************/

ACPI_STATUS AcpiOsTerminate(void)
{
	return (AE_OK);
}

/******************************************************************************
*
* FUNCTION:    AcpiOsGetRootPointer
*
* PARAMETERS:  None
*
* RETURN:      RSDP physical address
*
* DESCRIPTION: Gets the root pointer (RSDP)
*
*****************************************************************************/

/*
 The ACPI specification is highly portable specification, however, 
 it has a static part which is generally non-portable: the location of the Root System Descriptor Pointer. 
 This pointer may be found in many different ways depending on the chipset. 
 On PC-compatible computers (without EFI) it is located in lower memory generally 
 somewhere between 0x80000 and 0x100000. However, even within the PC compatible platform, 
 an EFI-enabled board will export the RSDP to the OS on when it loads it through the EFI system tables. 
 Other boards on server machines which are not PC-compatibles, 
 like embedded and handheld devices which implement ACPI will again, 
 not all be expected to position the RSDP in the same place as any other board. 
 The RSDP is therefore located in a chipset-specific manner; From the time the OS has the RSDP, 
 the rest of ACPI is completely portable. However, the way the RSDP is found is not. 
 This would be the reason that the ACPICA code wouldn't try to provide routines to expressly find the RSDP in a portable manner.
 */

ACPI_PHYSICAL_ADDRESS AcpiOsGetRootPointer(void)
{
	ACPI_PHYSICAL_ADDRESS Ret;
	AcpiFindRootPointer(&Ret);
	return Ret;
}

/******************************************************************************
*
* FUNCTION:    AcpiOsPredefinedOverride
*
* PARAMETERS:  InitVal             - Initial value of the predefined object
*              NewVal              - The new value for the object
*
* RETURN:      Status, pointer to value. Null pointer returned if not
*              overriding.
*
* DESCRIPTION: Allow the OS to override predefined names
*
*****************************************************************************/

ACPI_STATUS AcpiOsPredefinedOverride(
			const ACPI_PREDEFINED_NAMES *InitVal,
			ACPI_STRING                 *NewVal)
{

	if (!InitVal || !NewVal)
	{
		return (AE_BAD_PARAMETER);
	}

	*NewVal = NULL;
	return (AE_OK);
}

/******************************************************************************
*
* FUNCTION:    AcpiOsTableOverride
*
* PARAMETERS:  ExistingTable       - Header of current table (probably firmware)
*              NewTable            - Where an entire new table is returned.
*
* RETURN:      Status, pointer to new table. Null pointer returned if no
*              table is available to override
*
* DESCRIPTION: Return a different version of a table if one is available
*
*****************************************************************************/

ACPI_STATUS AcpiOsTableOverride(
			ACPI_TABLE_HEADER       *ExistingTable,
			ACPI_TABLE_HEADER       **NewTable)
{

	if (!ExistingTable || !NewTable) {
		return (AE_BAD_PARAMETER);
	}

	*NewTable = NULL;
	return (AE_OK);
}

/******************************************************************************
*
* FUNCTION:    AcpiOsPhysicalTableOverride
*
* PARAMETERS:  ExistingTable       - Header of current table (probably firmware)
*              NewAddress          - Where new table address is returned
*                                    (Physical address)
*              NewTableLength      - Where new table length is returned
*
* RETURN:      Status, address/length of new table. Null pointer returned
*              if no table is available to override.
*
* DESCRIPTION: Returns AE_SUPPORT.
*
*****************************************************************************/

ACPI_STATUS AcpiOsPhysicalTableOverride(
			ACPI_TABLE_HEADER       *ExistingTable,
			ACPI_PHYSICAL_ADDRESS   *NewAddress,
			UINT32                  *NewTableLength)
{
	return (AE_NOT_IMPLEMENTED);
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

ACPI_STATUS AcpiOsReadPciConfiguration(
			ACPI_PCI_ID             *PciId,
			UINT32                  Register,
			UINT64                  *Value,
			UINT32                  Width)
{
	switch (Width)
	{
		case 8:
		{
			*Value = (UINT64)PciRead8(PciId->Bus, PciId->Device, PciId->Function, Register);
		} break;

		case 16:
		{
			*Value = (UINT64)PciRead16(PciId->Bus, PciId->Device, PciId->Function, Register);
		} break;

		case 32:
		{
			*Value = (UINT64)PciRead32(PciId->Bus, PciId->Device, PciId->Function, Register);
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

ACPI_STATUS AcpiOsWritePciConfiguration(
			ACPI_PCI_ID             *PciId,
			UINT32                  Register,
			UINT64                  Value,
			UINT32                  Width)
{
	switch (Width)
	{
		case 8:
		{
			PciWrite8(PciId->Bus, PciId->Device, PciId->Function, Register, (UINT8)Value);
		} break;

		case 16:
		{
			PciWrite16(PciId->Bus, PciId->Device, PciId->Function, Register, (UINT16)Value);
		} break;

		case 32:
		{
			PciWrite32(PciId->Bus, PciId->Device, PciId->Function, Register, (UINT32)Value);
		} break;

		default:
			return (AE_ERROR);
	}

	return (AE_OK);
}

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
AcpiOsReadPort(
ACPI_IO_ADDRESS         Address,
UINT32                  *Value,
UINT32                  Width)
{
	ACPI_FUNCTION_NAME(OsReadPort);

	switch (Width)
	{
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

ACPI_STATUS AcpiOsWritePort(
ACPI_IO_ADDRESS         Address,
UINT32                  Value,
UINT32                  Width)
{
	ACPI_FUNCTION_NAME(OsWritePort);


	if ((Width == 8) || (Width == 16) || (Width == 32))
	{
		switch (Width)
		{
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
AcpiOsReadMemory(
ACPI_PHYSICAL_ADDRESS   Address,
UINT64                  *Value,
UINT32                  Width)
{

	switch (Width)
	{
	case 8:
	case 16:
	case 32:
	case 64:

		*Value = 0;
		break;

	default:

		return (AE_BAD_PARAMETER);
		break;
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

ACPI_STATUS AcpiOsWriteMemory(
ACPI_PHYSICAL_ADDRESS   Address,
UINT64                  Value,
UINT32                  Width)
{

	return (AE_OK);
}



/******************************************************************************
*
* FUNCTION:    AcpiOsPrintf
*
* PARAMETERS:  Fmt, ...            - Standard printf format
*
* RETURN:      None
*
* DESCRIPTION: Formatted output
*
*****************************************************************************/

void ACPI_INTERNAL_VAR_XFACE AcpiOsPrintf(const char *Fmt, ...)
{
	va_list		Args;
	
	va_start(Args, Fmt);
	AcpiOsVprintf(Fmt, Args);
	va_end(Args);
}


/******************************************************************************
*
* FUNCTION:    AcpiOsVprintf
*
* PARAMETERS:  Fmt                 - Standard printf format
*              Args                - Argument list
*
* RETURN:      None
*
* DESCRIPTION: Formatted output with argument list pointer
*
*****************************************************************************/
#define __MODULE "ACPI"
#define __TRACE
#include <debug.h>
void AcpiOsVprintf(const char *Fmt, va_list Args)
{
    // Temporary buffer
	char Buffer[512];
    memset(Buffer, 0, 512);
    
    // Format and trace it
	vsprintf(Buffer, Fmt, Args);
	TRACE(Buffer);
}

/******************************************************************************
*
* FUNCTION:    AcpiOsGetLine
*
* PARAMETERS:  Buffer              - Where to return the command line
*              BufferLength        - Maximum length of Buffer
*              BytesRead           - Where the actual byte count is returned
*
* RETURN:      Status and actual bytes read
*
* DESCRIPTION: Formatted input with argument list pointer
*
*****************************************************************************/

ACPI_STATUS
AcpiOsGetLine(
char                    *Buffer,
UINT32                  BufferLength,
UINT32                  *BytesRead)
{
	int                     Temp = EOF;
	UINT32                  i;


	for (i = 0;; i++)
	{
		if (i >= BufferLength)
		{
			return (AE_BUFFER_OVERFLOW);
		}

		/*
		if ((Temp = getchar()) == EOF)
		{
			return (AE_ERROR);
		}
		 */
		if (!Temp || Temp == '\n')
		{
			break;
		}

		Buffer[i] = (char)Temp;
	}

	/* Null terminate the buffer */

	Buffer[i] = 0;

	/* Return the number of bytes in the string */

	if (BytesRead)
	{
		*BytesRead = i;
	}
	return (AE_OK);
}

/******************************************************************************
*
* FUNCTION:    AcpiOsRedirectOutput
*
* PARAMETERS:  Destination         - An open file handle/pointer
*
* RETURN:      None
*
* DESCRIPTION: Causes redirect of AcpiOsPrintf and AcpiOsVprintf
*
*****************************************************************************/

void AcpiOsRedirectOutput(void *Destination)
{

	Acpi_RedirectionTarget = Destination;
}


/* Stubs for the disassembler */
#include "include\acdisasm.h"
void
MpSaveGpioInfo(
ACPI_PARSE_OBJECT       *Op,
AML_RESOURCE            *Resource,
UINT32                  PinCount,
UINT16                  *PinList,
char                    *DeviceName)
{
}

void
MpSaveSerialInfo(
ACPI_PARSE_OBJECT       *Op,
AML_RESOURCE            *Resource,
char                    *DeviceName)
{
}
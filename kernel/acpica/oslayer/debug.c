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
 * MollenOS MCore - ACPICA Support Layer (Debug Functions)
 *  - Missing implementations are todo
 */
#define __MODULE "ACPI"
#define __TRACE

/* Includes
 * - (OS) System */
#include <system/addresspace.h>
#include <system/utils.h>
#include <interrupts.h>
#include <threading.h>
#include <scheduler.h>
#include <debug.h>

/* Includes
 * - (ACPI) System */
#include <acpi.h>
#include <accommon.h>
#include <acdisasm.h>

/* Definitions
 * - Component Setup */
#define _COMPONENT ACPI_OS_SERVICES
ACPI_MODULE_NAME("oslayer_debug")

/* Definitions
 * - Global state variables */
char AcpiGbl_OutputBuffer[512];
void *AcpiGbl_RedirectionTarget = NULL;

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
void ACPI_INTERNAL_VAR_XFACE
AcpiOsPrintf (
    const char              *Format,
    ...)
{
    va_list	Args;
	va_start(Args, Format);
	AcpiOsVprintf(Format, Args);
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
void
AcpiOsVprintf (
    const char              *Format,
    va_list                 Args)
{
    // Temporary buffer
	char Buffer[256] = { 0 };
    vsprintf(Buffer, Format, Args);
    if (Buffer[0] == '\n') {
        TRACE(&AcpiGbl_OutputBuffer[0]);
        memset(&AcpiGbl_OutputBuffer[0], 0, sizeof(AcpiGbl_OutputBuffer));
    }
    else {
        strcat(&AcpiGbl_OutputBuffer[0], &Buffer[0]);
    }
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
void
AcpiOsRedirectOutput (
    void                    *Destination)
{
    AcpiGbl_RedirectionTarget = Destination;
}


/*
 * Debug IO
 */
ACPI_STATUS
AcpiOsInitializeDebugger (
    void)
{
    return AE_NOT_IMPLEMENTED;
}

void
AcpiOsTerminateDebugger (
    void)
{

}

ACPI_STATUS
AcpiOsWaitCommandReady (
    void)
{
    return AE_NOT_IMPLEMENTED;
}

ACPI_STATUS
AcpiOsNotifyCommandComplete (
    void)
{
    return AE_NOT_IMPLEMENTED;
}

void
AcpiOsTracePoint (
    ACPI_TRACE_EVENT_TYPE   Type,
    BOOLEAN                 Begin,
    UINT8                   *Aml,
    char                    *Pathname)
{

}

/*
 * Assembler IO
 */
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

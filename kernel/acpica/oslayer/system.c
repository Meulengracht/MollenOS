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
 * MollenOS MCore - ACPICA Support Layer (System Functions)
 *  - Missing implementations are todo
 */
#define __MODULE "ACPI"
#define __TRACE

/* Includes
 * - (OS) System */
#include <system/thread.h>
#include <system/utils.h>
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
ACPI_MODULE_NAME("oslayer_system")

/* Definitions 
 * Global state variables */
static UUId_t AcpiGbl_InterruptId[32] = { UUID_INVALID };
extern ACPI_OS_SEMAPHORE_INFO AcpiGbl_Semaphores[ACPI_OS_MAX_SEMAPHORES];
extern void *AcpiGbl_RedirectionTarget;
extern char AcpiGbl_OutputBuffer[512];
extern int AcpiGbl_DebugTimeout;

/******************************************************************************
 *
 * FUNCTION:    AcpiOsInitialize
 *
 * PARAMETERS:  None
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Initialize the OSL
 *
 *****************************************************************************/
ACPI_STATUS
AcpiOsInitialize (
    void)
{
    // Initialize globals
    memset(&AcpiGbl_Semaphores[0], 0, sizeof(AcpiGbl_Semaphores));
    memset(&AcpiGbl_OutputBuffer[0], 0, sizeof(AcpiGbl_OutputBuffer));
    memset(&AcpiGbl_InterruptId[0], UUID_INVALID, sizeof(AcpiGbl_InterruptId));
    AcpiGbl_RedirectionTarget = NULL;
    AcpiGbl_DebugTimeout = 0;
    return AE_OK;
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
ACPI_STATUS
AcpiOsTerminate (
    void)
{
    // Do cleanup, but not really as this only happens
    // on system shutdown
    return AE_OK;
}

/******************************************************************************
 *
 * FUNCTION:    AcpiOsInstallInterruptHandler
 *
 * PARAMETERS:  InterruptNumber     - Level handler should respond to.
 *              ServiceRoutine      - Address of the ACPI interrupt handler
 *              Context             - User context
 *
 * RETURN:      Handle to the newly installed handler.
 *
 * DESCRIPTION: Install an interrupt handler. Used to install the ACPI
 *              OS-independent handler.
 *
 *****************************************************************************/
ACPI_STATUS
AcpiOsInstallInterruptHandler (
    UINT32                  InterruptNumber,
    ACPI_OSD_HANDLER        ServiceRoutine,
    void                    *Context)
{
    // Variables
    MCoreInterrupt_t ACPIInterrupt;

    // Sanitize param
    if (InterruptNumber >= 32) {
        return AE_ERROR;
    }
    
    // Setup interrupt
    memset(&ACPIInterrupt, 0, sizeof(MCoreInterrupt_t));
	ACPIInterrupt.Data = Context;
	ACPIInterrupt.Line = InterruptNumber;
	ACPIInterrupt.Pin = INTERRUPT_NONE;
	ACPIInterrupt.Vectors[0] = InterruptNumber;
	ACPIInterrupt.Vectors[1] = INTERRUPT_NONE;
    ACPIInterrupt.FastHandler = (InterruptHandler_t)ServiceRoutine;

	// Install it
    AcpiGbl_InterruptId[InterruptNumber] = 
        InterruptRegister(&ACPIInterrupt, INTERRUPT_KERNEL);InterruptRegister(&ACPIInterrupt, INTERRUPT_KERNEL);
    if (AcpiGbl_InterruptId[InterruptNumber] != UUID_INVALID) {
        return AE_OK;
    }
	return AE_ERROR;
}

/******************************************************************************
 *
 * FUNCTION:    AcpiOsRemoveInterruptHandler
 *
 * PARAMETERS:  Handle              - Returned when handler was installed
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Uninstalls an interrupt handler.
 *
 *****************************************************************************/
ACPI_STATUS
AcpiOsRemoveInterruptHandler (
    UINT32                  InterruptNumber,
    ACPI_OSD_HANDLER        ServiceRoutine)
{
    // Sanitize stored id
    if (AcpiGbl_InterruptId[InterruptNumber] == UUID_INVALID) {
        return AE_ERROR;
    }

    // Uninstall
    if (InterruptUnregister(AcpiGbl_InterruptId[InterruptNumber]) != OsSuccess) {
        return AE_ERROR;
    }

    // Reset the id
    AcpiGbl_InterruptId[InterruptNumber] = UUID_INVALID;
	return AE_OK;
}

/******************************************************************************
 *
 * FUNCTION:    AcpiOsGetThreadId
 *
 * PARAMETERS:  None
 *
 * RETURN:      Id of the running thread
 *
 * DESCRIPTION: Get the Id of the current (running) thread
 *
 *****************************************************************************/
ACPI_THREAD_ID
AcpiOsGetThreadId (
    void)
{
    // Sanitize current threading status
    if (ThreadingIsEnabled() != 0) {
        return (ACPI_THREAD_ID)ThreadingGetCurrentThreadId();
    }
    return (ACPI_THREAD_ID)0;
}

/******************************************************************************
 *
 * FUNCTION:    AcpiOsExecute
 *
 * PARAMETERS:  Type                - Type of execution
 *              Function            - Address of the function to execute
 *              Context             - Passed as a parameter to the function
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Execute a new thread
 *
 *****************************************************************************/
ACPI_STATUS
AcpiOsExecute (
    ACPI_EXECUTE_TYPE       Type,
    ACPI_OSD_EXEC_CALLBACK  Function,
    void                    *Context)
{
    UUId_t Id = ThreadingCreateThread("acpi-worker", Function, Context, 0);
    if (Id != UUID_INVALID) {
        return AE_OK;
    }
    return AE_ERROR;
}

/******************************************************************************
 *
 * FUNCTION:    AcpiOsWaitEventsComplete
 *
 * PARAMETERS:  None
 *
 * RETURN:      None
 *
 * DESCRIPTION: Wait for all asynchronous events to complete.
 *
 *****************************************************************************/
void
AcpiOsWaitEventsComplete (
    void)
{
    // Do nothing
}

/******************************************************************************
 *
 * FUNCTION:    AcpiOsSleep
 *
 * PARAMETERS:  Milliseconds        - Time to sleep
 *
 * RETURN:      None. Blocks until sleep is completed.
 *
 * DESCRIPTION: Sleep at millisecond granularity
 *
 *****************************************************************************/
void
AcpiOsSleep (
    UINT64                  Milliseconds)
{
    if (ThreadingIsEnabled() != 0) {
        SchedulerThreadSleep(NULL, (size_t)Milliseconds);
    }
    else {
        AcpiOsStall(Milliseconds * 1000);
    }
}

/******************************************************************************
 *
 * FUNCTION:    AcpiOsStall
 *
 * PARAMETERS:  Microseconds        - Time to stall
 *
 * RETURN:      None. Blocks until stall is completed.
 *
 * DESCRIPTION: Sleep at microsecond granularity (1 Milli = 1000 Micro)
 *
 *****************************************************************************/
void
AcpiOsStall (
    UINT32                  Microseconds)
{
    // We never stall for less than 1 ms
    CpuStall((Microseconds / 1000) + 1);
}


/******************************************************************************
 *
 * FUNCTION:    AcpiOsReadable
 *
 * PARAMETERS:  Pointer             - Area to be verified
 *              Length              - Size of area
 *
 * RETURN:      TRUE if readable for entire length
 *
 * DESCRIPTION: Verify that a pointer is valid for reading
 *
 *****************************************************************************/
BOOLEAN
AcpiOsReadable (
    void                    *Pointer,
    ACPI_SIZE               Length)
{
    FATAL(FATAL_SCOPE_KERNEL, "AcpiOsReadable()");
    return FALSE;
}

/******************************************************************************
 *
 * FUNCTION:    AcpiOsWritable
 *
 * PARAMETERS:  Pointer             - Area to be verified
 *              Length              - Size of area
 *
 * RETURN:      TRUE if writable for entire length
 *
 * DESCRIPTION: Verify that a pointer is valid for writing
 *
 *****************************************************************************/
BOOLEAN
AcpiOsWritable (
    void                    *Pointer,
    ACPI_SIZE               Length)
{
    FATAL(FATAL_SCOPE_KERNEL, "AcpiOsWritable()");
    return FALSE;
}

/******************************************************************************
 *
 * FUNCTION:    AcpiOsGetTimer
 *
 * PARAMETERS:  None
 *
 * RETURN:      Current ticks in 100-nanosecond units
 *
 * DESCRIPTION: Get the value of a system timer
 *
 ******************************************************************************/
UINT64
AcpiOsGetTimer (
    void)
{
    return (UINT64)CpuGetTicks();
}

/******************************************************************************
 *
 * FUNCTION:    AcpiOsSignal
 *
 * PARAMETERS:  Function            - ACPICA signal function code
 *              Info                - Pointer to function-dependent structure
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Miscellaneous functions.
 *
 *****************************************************************************/
ACPI_STATUS
AcpiOsSignal (
    UINT32                  Function,
    void                    *Info)
{
    FATAL(FATAL_SCOPE_KERNEL, "AcpiOsSignal()");
    return AE_ERROR;
}

/******************************************************************************
 *
 * FUNCTION:    AcpiOsEnterSleep
 *
 * PARAMETERS:  SleepState          - Which sleep state to enter
 *              RegaValue           - Register A value
 *              RegbValue           - Register B value
 *
 * RETURN:      Status
 *
 * DESCRIPTION: A hook before writing sleep registers to enter the sleep
 *              state. Return AE_CTRL_SKIP to skip further sleep register
 *              writes.
 *
 *****************************************************************************/
ACPI_STATUS
AcpiOsEnterSleep (
    UINT8                   SleepState,
    UINT32                  RegaValue,
    UINT32                  RegbValue)
{
    // Not used in MollenOS
    return AE_OK;
}

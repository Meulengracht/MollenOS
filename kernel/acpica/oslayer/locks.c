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
 * MollenOS MCore - ACPICA Support Layer (Locking Functions)
 *  - Missing implementations are todo
 */
#define __MODULE "ACPI"
//#define __TRACE

/* Includes
 * - (OS) System */
#include <semaphore.h>
#include <debug.h>

/* Includes
 * - (ACPI) System */
#include <acpi.h>
#include <accommon.h>

/* Definitions
 * - Component Setup */
#define _COMPONENT ACPI_OS_SERVICES
ACPI_MODULE_NAME("oslayer_locks")

/* Definitions 
 * - Global state variables */
ACPI_OS_SEMAPHORE_INFO AcpiGbl_Semaphores[ACPI_OS_MAX_SEMAPHORES];
int AcpiGbl_DebugTimeout = 0;

/******************************************************************************
 *
 * FUNCTION:    Spinlock interfaces
 *
 * DESCRIPTION: Map these interfaces to one-valued semaphore interfaces
 *
 *****************************************************************************/
ACPI_STATUS
AcpiOsCreateLock(
    ACPI_SPINLOCK           *OutHandle)
{
    return AcpiOsCreateSemaphore(1, 1, OutHandle);
}

void
AcpiOsDeleteLock(
    ACPI_SPINLOCK           Handle)
{
    AcpiOsDeleteSemaphore(Handle);
}

ACPI_CPU_FLAGS
AcpiOsAcquireLock(
    ACPI_SPINLOCK           Handle)
{
    AcpiOsWaitSemaphore(Handle, 1, 0);
    return 0;
}

void
AcpiOsReleaseLock(
    ACPI_SPINLOCK           Handle,
    ACPI_CPU_FLAGS          Flags)
{
    AcpiOsSignalSemaphore(Handle, 1);
}

#ifdef ACPI_SINGLE_THREADED
/******************************************************************************
 *
 * FUNCTION:    Semaphore stub functions
 *
 * DESCRIPTION: Stub functions used for single-thread applications that do
 *              not require semaphore synchronization. Full implementations
 *              of these functions appear after the stubs.
 *
 *****************************************************************************/
ACPI_STATUS
AcpiOsCreateSemaphore(
    UINT32              MaxUnits,
    UINT32              InitialUnits,
    ACPI_HANDLE         *OutHandle)
{
    *OutHandle = (ACPI_HANDLE) 1;
    return (AE_OK);
}

ACPI_STATUS
AcpiOsDeleteSemaphore(
    ACPI_HANDLE         Handle)
{
    return (AE_OK);
}

ACPI_STATUS
AcpiOsWaitSemaphore(
    ACPI_HANDLE         Handle,
    UINT32              Units,
    UINT16              Timeout)
{
    return (AE_OK);
}

ACPI_STATUS
AcpiOsSignalSemaphore(
    ACPI_HANDLE         Handle,
    UINT32              Units)
{
    return (AE_OK);
}

#else
/******************************************************************************
 *
 * FUNCTION:    AcpiOsCreateSemaphore
 *
 * PARAMETERS:  MaxUnits            - Maximum units that can be sent
 *              InitialUnits        - Units to be assigned to the new semaphore
 *              OutHandle           - Where a handle will be returned
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Create an OS semaphore
 *
 *****************************************************************************/
ACPI_STATUS
AcpiOsCreateSemaphore(
    UINT32                  MaxUnits,
    UINT32                  InitialUnits,
    ACPI_SEMAPHORE          *OutHandle)
{
    // Variables
    Semaphore_t *Semaphore = NULL;
    UINT32 i = 0;
    ACPI_FUNCTION_NAME(OsCreateSemaphore);

    if (MaxUnits == ACPI_UINT32_MAX) {
        MaxUnits = 255;
    }

    if (InitialUnits == ACPI_UINT32_MAX) {
        InitialUnits = MaxUnits;
    }

    if (InitialUnits > MaxUnits) {
        return (AE_BAD_PARAMETER);
    }

    // Find an empty slot
    for (i = 0; i < ACPI_OS_MAX_SEMAPHORES; i++) {
        if (!AcpiGbl_Semaphores[i].OsHandle) {
            break;
        }
    }
    if (i >= ACPI_OS_MAX_SEMAPHORES) {
        ACPI_EXCEPTION ((AE_INFO, AE_LIMIT,
            "Reached max semaphores (%u), could not create",
            ACPI_OS_MAX_SEMAPHORES));
        return (AE_LIMIT);
    }

    // Allocate the semaphore
    Semaphore = SemaphoreCreate(InitialUnits);
    if (!Semaphore) {
        ACPI_ERROR ((AE_INFO, "Could not create semaphore"));
        return (AE_NO_MEMORY);
    }

    AcpiGbl_Semaphores[i].MaxUnits = (uint16_t) MaxUnits;
    AcpiGbl_Semaphores[i].CurrentUnits = (uint16_t) InitialUnits;
    AcpiGbl_Semaphores[i].OsHandle = Semaphore;

    TRACE("Handle=%u, Max=%u, Current=%u, OsHandle=%p\n",
        i, MaxUnits, InitialUnits, Semaphore);
    *OutHandle = (ACPI_SEMAPHORE)i;
    return (AE_OK);
}

/******************************************************************************
 *
 * FUNCTION:    AcpiOsDeleteSemaphore
 *
 * PARAMETERS:  Handle              - Handle returned by AcpiOsCreateSemaphore
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Delete an OS semaphore
 *
 *****************************************************************************/
ACPI_STATUS
AcpiOsDeleteSemaphore(
    ACPI_SEMAPHORE          Handle)
{
    // Variables
    UINT32 Index = (UINT32) Handle;

    if ((Index >= ACPI_OS_MAX_SEMAPHORES) ||
        !AcpiGbl_Semaphores[Index].OsHandle) {
        return (AE_BAD_PARAMETER);
    }

    SemaphoreDestroy(AcpiGbl_Semaphores[Index].OsHandle);
    AcpiGbl_Semaphores[Index].OsHandle = NULL;
    return (AE_OK);
}

/******************************************************************************
 *
 * FUNCTION:    AcpiOsWaitSemaphore
 *
 * PARAMETERS:  Handle              - Handle returned by AcpiOsCreateSemaphore
 *              Units               - How many units to wait for
 *              Timeout             - How long to wait
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Wait for units
 *
 *****************************************************************************/
ACPI_STATUS
AcpiOsWaitSemaphore(
    ACPI_SEMAPHORE          Handle,
    UINT32                  Units,
    UINT16                  Timeout)
{
    // Variables
    OsStatus_t WaitStatus;
    UINT32 Index = (UINT32) Handle;
    UINT32 OsTimeout = Timeout;
    ACPI_FUNCTION_ENTRY ();

    if ((Index >= ACPI_OS_MAX_SEMAPHORES) ||
        !AcpiGbl_Semaphores[Index].OsHandle) {
        return (AE_BAD_PARAMETER);
    }

    if (Units > 1) {
        ERROR("WaitSemaphore: Attempt to receive %u units\n", Units);
        return (AE_NOT_IMPLEMENTED);
    }

    if (Timeout == ACPI_WAIT_FOREVER) {
        OsTimeout = 0;
        if (AcpiGbl_DebugTimeout) {
            // The debug timeout will prevent hang conditions
            OsTimeout = ACPI_OS_DEBUG_TIMEOUT;
        }
    }
    else {
        // Add 10ms to account for clock tick granularity
        OsTimeout += 10;
    }

    WaitStatus = SemaphoreP(
        AcpiGbl_Semaphores[Index].OsHandle, OsTimeout);
    if (WaitStatus == OsError) {
        if (AcpiGbl_DebugTimeout) {
            ACPI_EXCEPTION ((AE_INFO, AE_TIME,
                "Debug timeout on semaphore 0x%04X (%ums)\n",
                Index, ACPI_OS_DEBUG_TIMEOUT));
        }
        return (AE_TIME);
    }

    if (AcpiGbl_Semaphores[Index].CurrentUnits == 0) {
        ACPI_ERROR ((AE_INFO,
            "%s - No unit received. Timeout 0x%X, OS_Status 0x%X",
            AcpiUtGetMutexName (Index), Timeout, WaitStatus));
        return (AE_OK);
    }

    AcpiGbl_Semaphores[Index].CurrentUnits--;
    return (AE_OK);
}

/******************************************************************************
 *
 * FUNCTION:    AcpiOsSignalSemaphore
 *
 * PARAMETERS:  Handle              - Handle returned by AcpiOsCreateSemaphore
 *              Units               - Number of units to send
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Send units
 *
 *****************************************************************************/
ACPI_STATUS
AcpiOsSignalSemaphore(
    ACPI_SEMAPHORE          Handle,
    UINT32                  Units)
{
    // Variables
    UINT32 Index = (UINT32) Handle;
    ACPI_FUNCTION_ENTRY();

    if (Index >= ACPI_OS_MAX_SEMAPHORES) {
        ERROR("SignalSemaphore: Index/Handle out of range: %2.2X\n", Index);
        return (AE_BAD_PARAMETER);
    }

    if (!AcpiGbl_Semaphores[Index].OsHandle) {
        ERROR("SignalSemaphore: Null OS handle, Index %2.2X\n", Index);
        return (AE_BAD_PARAMETER);
    }

    if (Units > 1) {
        ERROR("SignalSemaphore: Attempt to signal %u units, Index %2.2X\n", Units, Index);
        return (AE_NOT_IMPLEMENTED);
    }

    if ((AcpiGbl_Semaphores[Index].CurrentUnits + 1) >
        AcpiGbl_Semaphores[Index].MaxUnits) {
        ACPI_ERROR ((AE_INFO,
            "Oversignalled semaphore[%u]! Current %u Max %u",
            Index, AcpiGbl_Semaphores[Index].CurrentUnits,
            AcpiGbl_Semaphores[Index].MaxUnits));
        return (AE_LIMIT);
    }

    AcpiGbl_Semaphores[Index].CurrentUnits++;
    SemaphoreV(AcpiGbl_Semaphores[Index].OsHandle, (int)Units);
    return (AE_OK);
}
#endif

/*
 * Mutex primitives. May be configured to use semaphores instead via
 * ACPI_MUTEX_TYPE (see platform/acenv.h)
 */
#if (ACPI_MUTEX_TYPE != ACPI_BINARY_SEMAPHORE)

ACPI_STATUS
AcpiOsCreateMutex (
    ACPI_MUTEX              *OutHandle)
{
    
}

void
AcpiOsDeleteMutex (
    ACPI_MUTEX              Handle)
{

}

ACPI_STATUS
AcpiOsAcquireMutex (
    ACPI_MUTEX              Handle,
    UINT16                  Timeout)
{

}

void
AcpiOsReleaseMutex (
    ACPI_MUTEX              Handle)
{
    
}

#endif

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
 * MollenOS MCore - ACPI(CA) System Interface
 */

#define __MODULE "ACPI"
#define __TRACE

#include <acpiinterface.h>
#include <assert.h>
#include <debug.h>
#include <heap.h>

// Access to the embedded controller instance
extern AcpiEcdt_t EmbeddedController;

// Handler prototypes
extern UINT32 AcpiShutdownHandler(void *Context);
extern UINT32 AcpiSleepHandler(void *Context);
extern UINT32 AcpiRebootHandler(void);
extern void   AcpiBusNotifyHandler(ACPI_HANDLE Device, UINT32 NotifyType, void *Context);
extern void   AcpiEventHandler(UINT32 EventType, ACPI_HANDLE Device, UINT32 EventNumber, void* Context);

// OSI for acpi
extern UINT32 AcpiOsi(ACPI_STRING InterfaceName, UINT32 Supported);
extern void AcpiOsiSetup(const char *OsiString);
extern void AcpiOsiInstall(void);

// OSC for acpi
extern void AcpiInitializeOsc(void);

ACPI_STATUS
AcpiInstallHandlers(void)
{
    ACPI_STATUS Status;

    // Install default system memory handler
    Status = AcpiInstallAddressSpaceHandler(ACPI_ROOT_OBJECT, ACPI_ADR_SPACE_SYSTEM_MEMORY,
        ACPI_DEFAULT_HANDLER, NULL, NULL);
    if (Status != AE_SAME_HANDLER && ACPI_FAILURE(Status)) {
        return Status;
    }

    // Install default io memory handler
    Status = AcpiInstallAddressSpaceHandler(ACPI_ROOT_OBJECT, ACPI_ADR_SPACE_SYSTEM_IO,
        ACPI_DEFAULT_HANDLER, NULL, NULL);
    if (Status != AE_SAME_HANDLER && ACPI_FAILURE(Status)) {
        return Status;
    }

    // Install default pci space handler
    Status = AcpiInstallAddressSpaceHandler(ACPI_ROOT_OBJECT, ACPI_ADR_SPACE_PCI_CONFIG,
        ACPI_DEFAULT_HANDLER, NULL, NULL);
    if (Status != AE_SAME_HANDLER && ACPI_FAILURE(Status)) {
        return Status;
    }
    
    // Install default pci space handler
    Status = AcpiInstallAddressSpaceHandler(ACPI_ROOT_OBJECT, ACPI_ADR_SPACE_DATA_TABLE,
        ACPI_DEFAULT_HANDLER, NULL, NULL);
    if (Status != AE_SAME_HANDLER && ACPI_FAILURE(Status)) {
        return Status;
    }
    return AE_OK;
}

void
AcpiInitialize(void)
{
    ACPI_STATUS Status;

    // Create the ACPI namespace from ACPI tables
    TRACE(" > loading acpi tables");
    Status = AcpiLoadTables();
    if (ACPI_FAILURE(Status)) {
        FATAL(FATAL_SCOPE_KERNEL, "Failed LoadTables, %" PRIuIN "!", Status);
    }

    // Install the OSI strings that we respond to
    TRACE(" > initializing osi interface, windows/vali");
    Status = AcpiInstallInterfaceHandler(AcpiOsi);
    if (ACPI_FAILURE(Status)) {
        FATAL(FATAL_SCOPE_KERNEL, "Failed AcpiInstallInterfaceHandler, %" PRIuIN "!", Status);
    }
    AcpiOsiSetup("Windows 2009");
    AcpiOsiSetup("Windows 2013");
    AcpiOsiSetup("Vali 2018");
    AcpiOsiInstall();

    // Install default handlers for acpi before EnableSubsystem if we want
    // to perform any actions before enabling devices with _INI and _STA
    //TRACE(" > installing default handlers");
    //Status = AcpiInstallHandlers();
    //if (ACPI_FAILURE(Status)) {
    //    FATAL(FATAL_SCOPE_KERNEL, "Failed AcpiInstallHandlers, %" PRIuIN "!", Status);
    //}

    // Initialize the ACPI hardware
    TRACE(" > enabling acpi");
    Status = AcpiEnableSubsystem(ACPI_FULL_INITIALIZATION);
    if (ACPI_FAILURE(Status)) {
        FATAL(FATAL_SCOPE_KERNEL, "Failed AcpiEnableSubsystem, %" PRIuIN "!", Status);
    }
    
    // Initialize the ec if present
    if (EmbeddedController.Handle != NULL) {
        TRACE(" > initializing the embedded controller");
        // Retrieve handle and initialize handlers
        Status = AcpiGetHandle(ACPI_ROOT_OBJECT, &EmbeddedController.NsPath[0], &EmbeddedController.Handle);
        if (ACPI_SUCCESS(Status)){
            
        }
    }

    // Handles must be installed after enabling subsystems, but before
    // initializing all acpi-objects 
    TRACE(" > setup acpi handlers");
    AcpiInstallNotifyHandler(ACPI_ROOT_OBJECT, ACPI_SYSTEM_NOTIFY, AcpiBusNotifyHandler, NULL);
    AcpiInstallGlobalEventHandler(AcpiEventHandler, NULL);
    //AcpiInstallFixedEventHandler(ACPI_EVENT_POWER_BUTTON, acpi_shutdown, NULL);
    //AcpiInstallFixedEventHandler(ACPI_EVENT_SLEEP_BUTTON, acpi_sleep, NULL);
    //ACPI_BUTTON_TYPE_LID
    
    // Complete the ACPI namespace object initialization
    TRACE(" > initializing acpi namespace");
    Status = AcpiInitializeObjects(ACPI_FULL_INITIALIZATION);
    if (ACPI_FAILURE(Status)){
        FATAL(FATAL_SCOPE_KERNEL, "Failed AcpiInitializeObjects, %" PRIuIN "!", Status);
    }

    // Run _OSC on root, it should always be run after InitializeObjects
    AcpiInitializeOsc();
}

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
 * MollenOS MCore - ACPI(CA) Device Scan Interface
 */

#define __MODULE "DSIF"
#define __TRACE

/* Includes 
 * - System */
#include <acpiinterface.h>
#include <debug.h>
#include <hpet.h>
#include <heap.h>

/* Includes
 * - Library */
#include <stddef.h>

/* Globals */
Collection_t *GlbPciAcpiDevices = NULL;
int GlbBusCounter               = 0;

/* AcpiDeviceLookupBusRoutings
 * lookup a bridge device for the given bus that contains pci routings */
AcpiDevice_t*
AcpiDeviceLookupBusRoutings(
    _In_ int Bus)
{
    // Variables
    AcpiDevice_t *Dev = NULL;
    DataKey_t Key;
    int Index = 0;

    // Loop through buses
    while (1) {
        Key.Value = ACPI_BUS_ROOT_BRIDGE;
        Dev = (AcpiDevice_t*)CollectionGetDataByKey(GlbPciAcpiDevices, Key, Index);

        // Sanity, if this returns
        // null we are out of data
        if (Dev == NULL) {
            break;
        }

        // Match the bus 
        if (Dev->GlobalBus == Bus && Dev->Routings != NULL) {
            return Dev;
        }

        // Next bus
        Index++;
    }

    // Not found
    return NULL;
}

/* AcpiDeviceCreate
 * Retrieve all available information about the handle
 * and create a new device proxy for it. */
OsStatus_t
AcpiDeviceCreate(
    _In_ ACPI_HANDLE Handle,
    _In_ ACPI_HANDLE Parent,
    _In_ int Type)
{
    // Variables
    AcpiDevice_t *Device = NULL;
    ACPI_BUFFER Buffer = { 0 };
    ACPI_STATUS Status;
    DataKey_t Key;

    // Allocate a new instance
    Device = (AcpiDevice_t*)kmalloc(sizeof(AcpiDevice_t));
    memset(Device, 0, sizeof(AcpiDevice_t));

    // Store initial members
    Device->Handle = Handle;
    Device->Parent = Parent;
    Device->Type = Type; 

    // Lookup identifiers, supported features and the bus-numbers
    Status = AcpiDeviceGetBusId(Device, Type);
    if (ACPI_FAILURE(Status)) {
        WARNING("Failed to retrieve bus-id");
    }
    Status = AcpiDeviceGetFeatures(Device);
    if (ACPI_FAILURE(Status)) {
        WARNING("Failed to retrieve device-features");
    }
    Status = AcpiDeviceGetBusAndSegment(Device);
    if (ACPI_FAILURE(Status)) {
        WARNING("Failed to retrieve bus-location");
    }

    // Get the name, if it fails set to (null)
    Buffer.Length = 128;
    Buffer.Pointer = &Device->Name[0];
    Status = AcpiGetName(Handle, ACPI_FULL_PATHNAME, &Buffer);
    if (ACPI_FAILURE(Status)) {
        memset(&Device->Name[0], 0, 128);
        strcpy(&Device->Name[0], "(null)");
    }

    // Handle their current status based on type
    switch (Type) {
        case ACPI_BUS_TYPE_DEVICE:
        case ACPI_BUS_TYPE_PROCESSOR: {
            Status = AcpiDeviceGetStatus(Device);
            if (ACPI_FAILURE(Status)) {
                ERROR("Device %s failed its dynamic status check", Device->BusId);
            }
            if (!(Device->Status & ACPI_STA_DEVICE_PRESENT) &&
                !(Device->Status & ACPI_STA_DEVICE_FUNCTIONING)) {
                ERROR("Device %s is not present or functioning", Device->BusId);
            }
        } break;

        default: {
            Device->Status = ACPI_STA_DEVICE_PRESENT | ACPI_STA_DEVICE_ENABLED |
                ACPI_STA_DEVICE_UI | ACPI_STA_DEVICE_FUNCTIONING;
        } break;
    }

    // Now retrieve the HID, UID and address
    Status = AcpiDeviceGetHWInfo(Device, Parent, Type);
    if (ACPI_FAILURE(Status)) {
        ERROR("Failed to retrieve object information about device %s", Device->BusId);
    }

    // Convience function, attach our own device data with device
    Status = AcpiDeviceAttachData(Device, Type);
    if (ACPI_FAILURE(Status)) {
        ERROR("Failed to attach device-data");
    }

    // Convert the address field to device-location
    if (Device->Features & ACPI_FEATURE_ADR) {
        Device->PciLocation.Device = ACPI_HIWORD(ACPI_LODWORD(Device->Address));
        Device->PciLocation.Function = ACPI_LOWORD(ACPI_LODWORD(Device->Address));
        if (Device->PciLocation.Device > 31) {
            Device->PciLocation.Device = 0;
        }
        if (Device->PciLocation.Function > 8) {
            Device->PciLocation.Function = 0;
        }
    }
    
    // EC: PNP0C09
    // EC Batt: PNP0C0A
    // Smart Battery Ctrl HID: ACPI0001
    // Smart Battery HID: ACPI0002
    // Power Source (Has _PSR): ACPI0003
    // GPE Block Device: ACPI0006
    
    // Check for the following HId's:
    // PNP0A03 (PCI Bridge)
    // PNP0A08 (PCI Express Bridge)
    if (strncmp(Device->HId, "PNP0A03", 7) == 0 ||
        strncmp(Device->HId, "PNP0A08", 7) == 0) {
        // Steps are: 
        // 1 NegotiateOsControl
        // 2 Install pci-config address handler
        // 3 Derive final pci-id
        // 4 Enumerate
        //pci_negiotiate_os_control(device);
        if (strncmp(Device->HId, "PNP0A08", 7) == 0) {
            // No support in kernel for PCI-Express
            ERROR("Missing support for PCI-Express");
            Status = AE_NOT_IMPLEMENTED;
        }
        else {
            Status = AcpiInstallAddressSpaceHandler(
                Device->Handle, ACPI_ADR_SPACE_PCI_CONFIG, 
                ACPI_DEFAULT_HANDLER, NULL, NULL);
        }
        Status = AcpiHwDerivePciId(&Device->PciLocation, Device->Handle, NULL);
        
        // Store correct bus-nr
        Device->Type = ACPI_BUS_ROOT_BRIDGE;
        Device->GlobalBus = GlbBusCounter;
        GlbBusCounter++;
    }
    else {
        Device->Type = Type;
    }
    
    // Feature data checks like _PRT
    // This must be run after initalizing of the bridge if
    // the device is a pci bridge
    if (Device->Features & ACPI_FEATURE_PRT) {
        Status = AcpiDeviceGetIrqRoutings(Device);
        if (ACPI_FAILURE(Status)) {
            ERROR("Failed to retrieve pci irq routings from device %s (%u)", 
                Device->BusId, Status);
        }
    }
    
    // Setup GPE
    if (Device->Features & ACPI_FEATURE_PRW) {
        Status = AcpiDeviceParsePower(Device);
        if (ACPI_FAILURE(Status)) {
            ERROR("Failed to parse power resources from device %s (%u)", 
                Device->BusId, Status);
        }
        else {
            AcpiSetupGpeForWake(Device->Handle, 
                Device->PowerSettings.GpeHandle, Device->PowerSettings.GpeBit);
        }
    }

    // Add the device to device-list
    Key.Value = Device->Type;
    return CollectionAppend(GlbPciAcpiDevices, CollectionCreateNode(Key, Device));
}

/* AcpiDeviceInstallFixed 
 * Scans for fixed devices and initializes them. */
ACPI_STATUS
AcpiDeviceInstallFixed(void)
{
    // Variables
    ACPI_TABLE_HEADER *Header   = NULL;
    ACPI_STATUS Status          = AE_OK;

    // Check for HPET presence
    if (ACPI_SUCCESS(AcpiGetTable(ACPI_SIG_HPET, 0, &Header))) {
        TRACE("Initializing the hpet");
        Status = HpInitialize((ACPI_TABLE_HPET*)Header);
    }
    return Status;
}

/* AcpiDeviceScanCallback
 * Scan callback from the AcpiGetDevices on new device detection */
ACPI_STATUS
AcpiDeviceScanCallback(
    _In_ ACPI_HANDLE Handle,
    _In_ UINT32 Level,
    _In_ void *Context,
    _Out_ void **ReturnValue)
{
    // Variables
    ACPI_STATUS Status = AE_OK;
    ACPI_OBJECT_TYPE Type = 0;
    ACPI_HANDLE Parent = (ACPI_HANDLE)Context;

    // Lookup the type of device-handle
    Status = AcpiGetType(Handle, &Type);
    if (ACPI_FAILURE(Status)) {
        WARNING("Failed to enumerate device at level %u", Level);
        return AE_OK; // if it fails go to next
    }

    // The entire namespace is traversed and the_STA and _INI methods 
    // are run on all ACPI objects of type Device, Processor, and Thermal objects
    switch (Type) {
        case ACPI_TYPE_DEVICE:
            Type = ACPI_BUS_TYPE_DEVICE;
            break;
        case ACPI_TYPE_PROCESSOR:
            Type = ACPI_BUS_TYPE_PROCESSOR;
            break;
        case ACPI_TYPE_THERMAL:
            Type = ACPI_BUS_TYPE_THERMAL;
            break;
        case ACPI_TYPE_POWER:
            Type = ACPI_BUS_TYPE_PWM;
        default: {
            WARNING("Acpi gave us objects of type %u", Type);
            return AE_OK;
        }
    }

    // Retrieve the parent device handle
    Status = AcpiGetParent(Handle, &Parent);
    if (AcpiDeviceCreate(Handle, Parent, (int)Type) != OsSuccess) {
        ERROR("Failed to initialize acpi-device of type %u", Type);
    }
    return AE_OK;
}

/* AcpiDevicesScan
 * Scan the ACPI namespace for devices and irq-routings, 
 * this is very neccessary for getting correct irqs */
ACPI_STATUS
AcpiDevicesScan(void)
{
    // Debug
    TRACE("AcpiDevicesScan()");
    
    // Initialize list and fixed objects
    GlbPciAcpiDevices = CollectionCreate(KeyInteger);
    if (AcpiGbl_FADT.Flags & ACPI_FADT_POWER_BUTTON) {
        TRACE("Initializing power button");
        if (AcpiDeviceCreate(NULL, ACPI_ROOT_OBJECT, ACPI_BUS_TYPE_POWER) != OsSuccess) {
            ERROR("Failed to initialize power-button");
        }
    }
    if (AcpiGbl_FADT.Flags & ACPI_FADT_SLEEP_BUTTON) {
        TRACE("Initializing sleep button");
        if (AcpiDeviceCreate(NULL, ACPI_ROOT_OBJECT, ACPI_BUS_TYPE_SLEEP) != OsSuccess) {
            ERROR("Failed to initialize sleep-button");
        }
    }

    // Run device scan and update Gpes
    if (AcpiGetDevices(NULL, AcpiDeviceScanCallback, NULL, NULL) == AE_OK) {
        if (AcpiUpdateAllGpes() != AE_OK) {
            FATAL(FATAL_SCOPE_KERNEL, "Failed to update Gpes");
        }
        return AcpiDeviceInstallFixed();
    }
    else {
        FATAL(FATAL_SCOPE_KERNEL, "Failed to scan the ACPI namespace.");
        return AE_ERROR;
    }
}

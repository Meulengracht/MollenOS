/**
 * MollenOS
 *
 * Copyright 2015, Philip Meulengracht
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
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * ACPI(CA) Device Scan Interface
 */

#define __MODULE "ACPI"
//#define __TRACE

#include <acpiinterface.h>
#include <debug.h>
#include <hpet.h>
#include <heap.h>

extern ACPI_STATUS AcpiHwDerivePciId(ACPI_PCI_ID *PciId, ACPI_HANDLE RootPciDevice, ACPI_HANDLE PciRegion);

// Static storage for the pci-acpi mappings
static list_t g_acpiDevices = LIST_INIT;
static int    g_busCounter  = 0;

struct FindBusRoutings {
    int           Bus;
    AcpiDevice_t* Device;
};

static int
BusRoutingLookupCallback(
    _In_ int        i,
    _In_ element_t* element,
    _In_ void*      context)
{
    AcpiDevice_t*           device      = element->value;
    struct FindBusRoutings* findContext = context;
    
    if (device->Type == ACPI_BUS_ROOT_BRIDGE &&
        device->GlobalBus == findContext->Bus &&
        device->Routings != NULL)
    {
        findContext->Device = device;
        return LIST_ENUMERATE_STOP;
    }
    return LIST_ENUMERATE_CONTINUE;
}

AcpiDevice_t*
AcpiDeviceLookupBusRoutings(
    _In_ int bus)
{
    struct FindBusRoutings context = { .Bus = bus };
    list_enumerate(&g_acpiDevices, BusRoutingLookupCallback, &context);
    return context.Device;
}

oserr_t
AcpiDeviceCreate(
    _In_ ACPI_HANDLE deviceHandle,
    _In_ ACPI_HANDLE parentHandle,
    _In_ int         type)
{
    AcpiDevice_t* acpiDevice;
    ACPI_BUFFER   acpiBuffer = { 0 };
    ACPI_STATUS   acpiStatus;
    
    TRACE("AcpiDeviceCreate(deviceHandle=0x%" PRIxIN ", parentHandle=0x%" PRIxIN ", type=%i)",
          deviceHandle, parentHandle, type);

    acpiDevice = (AcpiDevice_t*)kmalloc(sizeof(AcpiDevice_t));
    if (!acpiDevice) {
        return OS_EOOM;
    }
    
    memset(acpiDevice, 0, sizeof(AcpiDevice_t));

    // Store initial members
    acpiDevice->Handle = deviceHandle;
    acpiDevice->Parent = parentHandle;
    acpiDevice->Type   = type;
    ELEMENT_INIT(&acpiDevice->Header, deviceHandle, acpiDevice);

    // Lookup identifiers, supported features and the bus-numbers
    acpiStatus = AcpiDeviceGetBusId(acpiDevice, type);
    if (ACPI_FAILURE(acpiStatus)) {
        WARNING("Failed to retrieve bus-id");
    }

    acpiStatus = AcpiDeviceGetFeatures(acpiDevice);
    if (ACPI_FAILURE(acpiStatus)) {
        WARNING("Failed to retrieve device-features");
    }

    acpiStatus = AcpiDeviceGetBusAndSegment(acpiDevice);
    if (ACPI_FAILURE(acpiStatus)) {
        WARNING("Failed to retrieve bus-location");
    }

    // Get the name, if it fails set to (null)
    acpiBuffer.Length  = 128;
    acpiBuffer.Pointer = &acpiDevice->Name[0];
    acpiStatus = AcpiGetName(deviceHandle, ACPI_FULL_PATHNAME, &acpiBuffer);
    if (ACPI_FAILURE(acpiStatus)) {
        memset(&acpiDevice->Name[0], 0, 128);
        strcpy(&acpiDevice->Name[0], "(null)");
    }

    // Handle their current status based on type
    TRACE("AcpiDeviceCreate name=%s, features=0x%x", acpiDevice->Name, acpiDevice->Features);
    switch (type) {
        case ACPI_BUS_TYPE_DEVICE:
        case ACPI_BUS_TYPE_PROCESSOR: {
            acpiStatus = AcpiDeviceGetStatus(acpiDevice);
            if (ACPI_FAILURE(acpiStatus)) {
                ERROR("Device %s failed its dynamic status check", acpiDevice->BusId);
            }
            if (!(acpiDevice->Status & ACPI_STA_DEVICE_PRESENT) &&
                !(acpiDevice->Status & ACPI_STA_DEVICE_FUNCTIONING)) {
                ERROR("Device %s is not present or functioning", acpiDevice->BusId);
            }
        } break;

        default: {
            acpiDevice->Status = ACPI_STA_DEVICE_PRESENT | ACPI_STA_DEVICE_ENABLED |
                                 ACPI_STA_DEVICE_UI | ACPI_STA_DEVICE_FUNCTIONING;
        } break;
    }

    // Now retrieve the HID, UID and address
    acpiStatus = AcpiDeviceGetHWInfo(acpiDevice, parentHandle, type);
    if (ACPI_FAILURE(acpiStatus)) {
        ERROR("Failed to retrieve object information about device %s", acpiDevice->BusId);
    }

    // Convience function, attach our own device data with device
    acpiStatus = AcpiDeviceAttachData(acpiDevice, type);
    if (ACPI_FAILURE(acpiStatus)) {
        ERROR("Failed to attach device-data");
    }

    // Convert the address field to device-location
    if (acpiDevice->Features & ACPI_FEATURE_ADR) {
        acpiDevice->PciLocation.Device   = ACPI_HIWORD(ACPI_LODWORD(acpiDevice->Address));
        acpiDevice->PciLocation.Function = ACPI_LOWORD(ACPI_LODWORD(acpiDevice->Address));
        if (acpiDevice->PciLocation.Device > 31) {
            acpiDevice->PciLocation.Device = 0;
        }
        if (acpiDevice->PciLocation.Function > 8) {
            acpiDevice->PciLocation.Function = 0;
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
    if (strncmp(acpiDevice->HId, "PNP0A03", 7) == 0 ||
        strncmp(acpiDevice->HId, "PNP0A08", 7) == 0) {
        // Steps are: 
        // 1 NegotiateOsControl
        // 2 Install pci-config address handler
        // 3 Derive final pci-id
        // 4 Enumerate
        //pci_negiotiate_os_control(device);
        if (strncmp(acpiDevice->HId, "PNP0A08", 7) == 0) {
            // No support in kernel for PCI-Express
            ERROR("Missing support for PCI-Express");
            acpiStatus = AE_NOT_IMPLEMENTED;
        }
        else {
            acpiStatus = AcpiInstallAddressSpaceHandler(
                    acpiDevice->Handle, ACPI_ADR_SPACE_PCI_CONFIG,
                    ACPI_DEFAULT_HANDLER, NULL, NULL);
        }
        acpiStatus = AcpiHwDerivePciId(&acpiDevice->PciLocation, acpiDevice->Handle, NULL);
        
        // Store correct bus-nr
        acpiDevice->Type      = ACPI_BUS_ROOT_BRIDGE;
        acpiDevice->GlobalBus = g_busCounter++;
    }
    else {
        acpiDevice->Type = type;
    }
    
    // Feature data checks like _PRT
    // This must be run after initalizing of the bridge if
    // the device is a pci bridge
    if (acpiDevice->Features & ACPI_FEATURE_PRT) {
        acpiStatus = AcpiDeviceGetIrqRoutings(acpiDevice);
        if (ACPI_FAILURE(acpiStatus)) {
            ERROR("Failed to retrieve pci irq routings from device %s (%" PRIuIN ")",
                  acpiDevice->BusId, acpiStatus);
        }
    }
    
    // Setup GPE
    if (acpiDevice->Features & ACPI_FEATURE_PRW) {
        acpiStatus = AcpiDeviceParsePower(acpiDevice);
        if (ACPI_FAILURE(acpiStatus)) {
            ERROR("AcpiDeviceCreate parse_power_package failed for device %s (%" PRIuIN ")",
                  acpiDevice->BusId, acpiStatus);
        }
        else {
            AcpiSetupGpeForWake(acpiDevice->Handle,
                                acpiDevice->PowerSettings.GpeHandle, acpiDevice->PowerSettings.GpeBit);
        }
    }

    list_append(&g_acpiDevices, &acpiDevice->Header);
    TRACE("AcpiDeviceCreate returns=0");
    return OS_EOK;
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
        WARNING("Failed to enumerate device at level %" PRIuIN "", Level);
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
            WARNING("Acpi gave us objects of type %" PRIuIN "", Type);
            return AE_OK;
        }
    }

    // Retrieve the parent device handle
    Status = AcpiGetParent(Handle, &Parent);
    if (AcpiDeviceCreate(Handle, Parent, (int)Type) != OS_EOK) {
        ERROR("Failed to initialize acpi-device of type %" PRIuIN "", Type);
    }
    return AE_OK;
}

ACPI_STATUS
AcpiDevicesScan(void)
{
    TRACE("AcpiDevicesScan()");
    
    // Initialize list and fixed objects
    if (AcpiGbl_FADT.Flags & ACPI_FADT_POWER_BUTTON) {
        TRACE("Initializing power button");
        if (AcpiDeviceCreate(NULL, ACPI_ROOT_OBJECT, ACPI_BUS_TYPE_POWER) != OS_EOK) {
            ERROR("Failed to initialize power-button");
        }
    }
    if (AcpiGbl_FADT.Flags & ACPI_FADT_SLEEP_BUTTON) {
        TRACE("Initializing sleep button");
        if (AcpiDeviceCreate(NULL, ACPI_ROOT_OBJECT, ACPI_BUS_TYPE_SLEEP) != OS_EOK) {
            ERROR("Failed to initialize sleep-button");
        }
    }

    // Run device scan and update Gpes
    if (AcpiGetDevices(NULL, AcpiDeviceScanCallback, NULL, NULL) == AE_OK) {
        if (AcpiUpdateAllGpes() != AE_OK) {
            FATAL(FATAL_SCOPE_KERNEL, "Failed to update Gpes");
        }
        return AE_OK;
    }
    else {
        FATAL(FATAL_SCOPE_KERNEL, "Failed to scan the ACPI namespace.");
        return AE_ERROR;
    }
}

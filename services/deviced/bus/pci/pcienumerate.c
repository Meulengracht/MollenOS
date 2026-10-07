/**
 * MollenOS
 *
 * Copyright (C) Philip Meulengracht
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
 */

//#define __TRACE

#include <assert.h>
#include "bus.h"
#include <devices.h>
#include <ddk/acpi.h>
#include <ddk/busdevice.h>
#include <ddk/firmware.h>
#include <ddk/interrupt.h>
#include <ddk/utils.h>
#include <firmware/pci.h>
#include <stdlib.h>
#include <ds/list.h>
#include <threads.h>

#include "hosts/broadcom/bcm.h"

#define DEVICE_IS_PCI_BRIDGE(device) ((device)->Header->Class == PCI_CLASS_BRIDGE && (device)->Header->Subclass == PCI_BRIDGE_SUBCLASS_PCI)

void PciCheckBus(PciDevice_t* parent, int bus);

#ifdef __OSCONFIG_HAS_LEGACY_PCI
oserr_t __InstallPS2Controller(void);
#endif

static list_t g_pciDevices;
static mtx_t  g_pciDevicesLock;
static list_t g_pciRoots = LIST_INIT;
static int    g_acpiAvailable = 0;
static uuid_t g_nextPciHostId = 1;

struct PciFirmwareMapping {
    const void*  Blob;
    size_t       Length;
    unsigned int References;
};

void PciCriticalSectionEnter(void)
{
    mtx_lock(&g_pciDevicesLock);
}

void PciCriticalSectionLeave(void)
{
    mtx_unlock(&g_pciDevicesLock);
}

int
PciIsAcpiAvailable(void)
{
    return g_acpiAvailable;
}

PciDevice_t*
PciFindDevice(
    _In_ unsigned int segment,
    _In_ unsigned int bus,
    _In_ unsigned int slot,
    _In_ unsigned int function)
{
    PciDevice_t* device;

    foreach (element, &g_pciDevices) {
        device = element->value;
        if ((unsigned int)device->Host->Identification.Segment == segment &&
            device->Bus == bus && device->Slot == slot && device->Function == function) {
            return device;
        }
    }
    return NULL;
}

static void
__PciReleaseFirmware(
    _In_ struct PciFirmwareMapping* mapping)
{
    if (mapping != NULL && --mapping->References == 0) {
        FirmwareTableUnmap(mapping->Blob, mapping->Length);
        free(mapping);
    }
}

static void
__PciDestroyDevice(
    _In_ PciDevice_t* device)
{
    while (device->children.head != NULL) {
        __PciDestroyDevice(device->children.head->value);
    }
    
    list_remove(&device->Parent->children, &device->child_header);
    list_remove(&g_pciDevices, &device->list_header);
    
    if (device->Attachment != NULL) {
        device->Handler->Destroy(device->Attachment);
    }
    free(device->Header);
    free(device);
}

void
PciHostDestroy(
    _In_ PciHost_t* bus)
{
    if (bus == NULL) {
        return;
    }
    
    // Callers have stopped this host's clients before its tree is removed.
    PciCriticalSectionEnter();
    
    if (bus->RootDevice != NULL) {
        while (bus->RootDevice->children.head != NULL) {
            __PciDestroyDevice(bus->RootDevice->children.head->value);
        }
        list_remove(&g_pciRoots, &bus->RootDevice->list_header);
        free(bus->RootDevice);
        bus->RootDevice = NULL;
    }
    
    if (bus->Operations != NULL && bus->Operations->Destroy != NULL) {
        bus->Operations->Destroy(bus);
    }
    
    ReleaseDeviceIo(&bus->IoSpace);
    DestroyDeviceIo(&bus->IoSpace);
    __PciReleaseFirmware(bus->FirmwareMapping);
    free(bus);

    PciCriticalSectionLeave();
}

oserr_t
PciHostRegister(
    _In_ PciHost_t* host)
{
    PciDevice_t*                        root;
    PciHost_t*                          existing;
    const struct PciHostIdentification* identification;
    struct FdtPciMsi                    msi;
    DeviceMsiControllerDescription_t    msiController;
    uuid_t                              msiControllerId = UUID_INVALID;
    oserr_t                             status;

    if (host == NULL || host->Operations == NULL) {
        return OS_EINVALPARAMS;
    }
    if (host->Operations->Read == NULL || host->Operations->Write == NULL) {
        return OS_EINVALPARAMS;
    }
    identification = &host->Identification;
    if (identification->BusStart > identification->BusEnd) {
        return OS_EINVALPARAMS;
    }

    PciCriticalSectionEnter();
    if (identification->HostId != UUID_INVALID || host->RootDevice != NULL) {
        PciCriticalSectionLeave();
        return OS_EEXISTS;
    }
    foreach (element, &g_pciRoots) {
        existing = ((PciDevice_t*)element->value)->Host;
        if (existing->Identification.Segment != identification->Segment) {
            continue;
        }
        if (identification->BusStart <= existing->Identification.BusEnd &&
            existing->Identification.BusStart <= identification->BusEnd) {
            PciCriticalSectionLeave();
            return OS_EEXISTS;
        }
    }
    if (g_nextPciHostId == UUID_INVALID) {
        PciCriticalSectionLeave();
        return OS_EOVERFLOW;
    }

    root = calloc(1, sizeof(*root));
    if (root == NULL) {
        PciCriticalSectionLeave();
        return OS_EOOM;
    }

    if (host->Firmware != NULL) {
        status = FdtResolvePciMsi(host->Firmware, &msi);
        if (status == OS_EOK && msi.IsMip) {
            memset(&msiController, 0, sizeof(msiController));
            msiController.Type = DEVICE_MSI_CONTROLLER_MIP;
            msiController.ProviderId = msi.Controller;
            msiController.Segment = identification->Segment;
            msiController.BusStart = identification->BusStart;
            msiController.BusEnd = identification->BusEnd;
            msiController.ParentLine = msi.Interrupt.Line;
            msiController.MessageOffset = msi.Offset;
            msiController.MessageCount = msi.InterruptCount;
            msiController.DoorbellAddress = msi.DoorbellBase;
            msiController.DoorbellLength = msi.DoorbellLength;
            msiControllerId = DeviceInterruptMsiControllerRegister(&msiController);
            if (msiControllerId == UUID_INVALID) {
                PciCriticalSectionLeave();
                free(root);
                return OS_EUNKNOWN;
            }
        } else if (status != OS_EOK && status != OS_ENOENT && status != OS_ENOTSUPPORTED) {
            PciCriticalSectionLeave();
            free(root);
            return status;
        }
    }

    // Commit identification only after every operation that can fail. IDs are
    // not reused when a host is removed, including across discovery passes.
    host->Identification.HostId = g_nextPciHostId++;
    host->MsiControllerId = msiControllerId;
    list_construct(&root->children);
    ELEMENT_INIT(&root->list_header, (uintptr_t)identification->HostId, root);
    root->Host = host;
    root->Bus = identification->BusStart;
    root->IsBridge = 1;
    host->RootDevice = root;
    list_append(&g_pciRoots, &root->list_header);
    PciCriticalSectionLeave();
    return OS_EOK;
}

static oserr_t
__PciAttachHost(
    _In_ PciHost_t*                 bus,
    _In_ struct PciFirmwareMapping* mapping)
{
    oserr_t status;

    // The constructor borrows the discovery mapping. Establish the host's
    // reference before registration transfers ownership to the PCI subsystem.
    bus->FirmwareMapping = mapping;
    if (mapping != NULL) {
        mapping->References++;
    }
    status = PciHostRegister(bus);
    if (status != OS_EOK) {
        ERROR("PCI host registration failed for segment %u buses %u-%u: %u",
            bus->Identification.Segment, bus->Identification.BusStart,
            bus->Identification.BusEnd, status);
        bus->FirmwareMapping = NULL;
        __PciReleaseFirmware(mapping);
    }
    return status;
}

#ifdef __OSCONFIG_HAS_LEGACY_PCI
static void __EnumerateLegacy(void)
{
    PciHost_t* bus;
    oserr_t   oserr;
    int       function;

    bus = (PciHost_t*)malloc(sizeof(PciHost_t));
    if (!bus) {
        return;
    }
    memset(bus, 0, sizeof(PciHost_t));
    bus->Identification.BusEnd = 255;
    bus->Operations = &g_pciLegacyOperations;
    bus->IoResourcePolicy = PciIoResourcePorts;

    oserr = CreateDevicePortIo(&bus->IoSpace, PCI_IO_BASE, PCI_IO_LENGTH);
    if (oserr != OS_EOK) {
        ERROR(" > failed to initialize pci io space");
        free(bus);
        return;
    }

    oserr = AcquireDeviceIo(&bus->IoSpace);
    if (oserr != OS_EOK) {
        ERROR(" > failed to acquire pci io space");
        DestroyDeviceIo(&bus->IoSpace);
        free(bus);
        return;
    }
    if (__PciAttachHost(bus, NULL) != OS_EOK) {
        PciHostDestroy(bus);
        return;
    }

    // We can check whether or not it's a multi-function
    // root-bridge, in that case there are multiple buses
    if (!(PciReadHeaderType(bus, 0, 0, 0) & 0x80)) {
        PciCheckBus(bus->RootDevice, 0);
    }
    else {
        for (function = 0; function < 8; function++) {
            if (PciReadVendorId(bus, 0, 0, function) != 0xFFFF)
                break;
            PciCheckBus(bus->RootDevice, function);
        }
    }
}
#endif

// <base> is the ECAM address of <busStart>, each bus decodes 1MB.
static void
__EnumerateEcamWindow(
    _In_ uint32_t                   segment,
    _In_ uint8_t                    busStart,
    _In_ uint8_t                    busEnd,
    _In_ uint64_t                   base,
    _In_ const struct FdtPciHost*   firmwareHost,
    _In_ struct PciFirmwareMapping* mapping)
{
    PciHost_t* bus;
    size_t    length;

    if (busStart > busEnd) {
        return;
    }
    length = (size_t)(busEnd - busStart + 1) << 20;

    TRACE("ECAM segment %u, buses %u-%u at 0x%llx", segment, busStart, busEnd, base);
    if ((uint64_t)(uintptr_t)base != base || length - 1 > UINTPTR_MAX - (uintptr_t)base) {
        ERROR(" > pcie address space is not addressable");
        return;
    }

    bus = (PciHost_t*)malloc(sizeof(PciHost_t) +
            (firmwareHost != NULL ? sizeof(struct FdtPciHost) : 0));
    if (!bus) {
        return;
    }
    memset(bus, 0, sizeof(PciHost_t));

    if (CreateDeviceMemoryIo(&bus->IoSpace, (uintptr_t)base, length) != OS_EOK) {
        ERROR(" > failed to create pcie address space");
        free(bus);
        return;
    }

    if (AcquireDeviceIo(&bus->IoSpace) != OS_EOK) {
        ERROR(" > failed to map pcie address space");
        DestroyDeviceIo(&bus->IoSpace);
        free(bus);
        return;
    }

    bus->IsExtended = 1;
    bus->Identification.BusStart = busStart;
    bus->Identification.BusEnd = busEnd;
    bus->Identification.Segment = segment;
    bus->Operations = &g_pciAcpiEcamOperations;

#ifdef __OSCONFIG_HAS_LEGACY_PCI
    bus->IoResourcePolicy = PciIoResourcePorts;
#else
    bus->IoResourcePolicy = PciIoResourceMemory;
#endif
    
    if (firmwareHost != NULL) {
        bus->OpContext = bus + 1;
        memcpy(bus->OpContext, firmwareHost, sizeof(struct FdtPciHost));
        bus->Operations = &g_pciDtEcamOperations;
        bus->IoResourcePolicy = PciIoResourceMemory;
        bus->Firmware = bus->OpContext;
    }
    if (__PciAttachHost(bus, mapping) != OS_EOK) {
        PciHostDestroy(bus);
        return;
    }

    // Secondary buses are reached through bridges from the root bus.
    PciCheckBus(bus->RootDevice, busStart);
}

static void __EnumerateMcfg(void)
{
    ACPI_TABLE_HEADER*    header;
    ACPI_MCFG_ALLOCATION* entry;
    size_t                count;

    if (AcpiQueryTable(ACPI_SIG_MCFG, &header) != OS_EOK) {
        WARNING("BusEnumerate ACPI system without MCFG");
        return;
    }

    if (header->Length < sizeof(ACPI_TABLE_MCFG)) {
        free(header);
        return;
    }

    entry = (ACPI_MCFG_ALLOCATION*)((uint8_t*)header + sizeof(ACPI_TABLE_MCFG));
    count = (header->Length - sizeof(ACPI_TABLE_MCFG)) / sizeof(ACPI_MCFG_ALLOCATION);
    for (size_t i = 0; i < count; i++, entry++) {
        if (entry->EndBusNumber < entry->StartBusNumber) {
            continue;
        }
        if (entry->Address > UINT64_MAX - ((uint64_t)entry->StartBusNumber << 20)) {
            continue;
        }

        // MCFG addresses are relative to bus 0
        __EnumerateEcamWindow(
            entry->PciSegment, entry->StartBusNumber, entry->EndBusNumber,
            entry->Address + ((uint64_t)entry->StartBusNumber << 20), NULL, NULL
        );
    }
    free(header);
}

static void __OnFdtPciHost(
    _In_ const struct FdtPciHost* host,
    _In_ void*                    context)
{
    PciHost_t*                  bus;
    struct BcmPciHost*          controller;
    const struct BcmPciVariant* variant;
    oserr_t                     status;

    if (host->Type == FdtPciHostEcam) {
        __EnumerateEcamWindow(
            host->Segment,
            host->BusStart,
            host->BusEnd,
            host->EcamBase,
            host,
            context
        );
        return;
    }
    
    variant = BcmPciGetVariant(host->Type);
    if (variant == NULL) {
        WARNING("PCI host segment %u requires its own controller variant (type %u)",
            host->Segment, host->Type);
        return;
    }
    
    if (host->EcamBase > UINTPTR_MAX || host->EcamLength < variant->RegisterLength ||
        host->EcamLength > SIZE_MAX || host->EcamLength - 1 > UINTPTR_MAX - host->EcamBase) {
        return;
    }
    
    bus = calloc(1, sizeof(PciHost_t) + sizeof(struct BcmPciHost));
    if (bus == NULL) {
        return;
    }
    
    controller = (struct BcmPciHost*)(bus + 1);
    status = CreateDeviceMemoryIo(
        &bus->IoSpace,
        (uintptr_t)host->EcamBase,
        (size_t)host->EcamLength
    );
    if (status != OS_EOK) {
        free(bus);
        return;
    }
    
    status = AcquireDeviceIo(&bus->IoSpace);
    if (status != OS_EOK) {
        DestroyDeviceIo(&bus->IoSpace);
        free(bus);
        return;
    }
    
    status = BcmPciInitialize(bus, controller, host);
    if (status != OS_EOK) {
        ERROR("Broadcom PCIe segment %u initialization failed: %u", host->Segment, status);
        ReleaseDeviceIo(&bus->IoSpace);
        DestroyDeviceIo(&bus->IoSpace);
        free(bus);
        return;
    }
    if (__PciAttachHost(bus, context) != OS_EOK) {
        PciHostDestroy(bus);
        return;
    }
    
    PciCheckBus(bus->RootDevice, host->BusStart);
    WARNING("Broadcom PCIe enumerated; device drivers remain blocked pending DMA/INTx acceptance");
}

static void __EnumerateDeviceTree(void)
{
    OSFirmwareTableKey_t       key = { .Source = OSFIRMWARE_DEVICETREE };
    const void*                blob;
    size_t                     length;
    struct PciFirmwareMapping* mapping;
    oserr_t                    oserr;

    oserr = FirmwareTableMap(&key, &blob, &length);
    if (oserr != OS_EOK) {
        ERROR("BusEnumerate failed to map the device tree: %u", oserr);
        return;
    }

    mapping = calloc(1, sizeof(*mapping));
    if (mapping == NULL) {
        FirmwareTableUnmap(blob, length);
        return;
    }
    
    mapping->Blob = blob;
    mapping->Length = length;
    mapping->References = 1;
    
    oserr = FdtEnumeratePciHosts(blob, length, __OnFdtPciHost, mapping);
    if (oserr != OS_EOK) {
        ERROR("BusEnumerate malformed device tree: %u", oserr);
    } else if (mapping->References == 1) {
        WARNING("BusEnumerate could not initialize any device tree PCI hosts");
    }
    
    __PciReleaseFirmware(mapping);
}

void BusEnumerate(void)
{
    AcpiDescriptor_t acpi = { 0 };
    OSFirmwareInfo_t firmware = { 0 };
    element_t*       element;

    // Initialize the flat device list; each host owns a separate root tree.
    list_construct(&g_pciDevices);
    list_construct(&g_pciRoots);
    mtx_init(&g_pciDevicesLock, mtx_plain);
    g_acpiAvailable = 0;

    if (AcpiQueryStatus(&acpi) == OS_EOK) {
        TRACE("ACPI-Version: 0x%x (BootFlags 0x%x)",
              acpi.Version, acpi.BootFlags);
        g_acpiAvailable = 1;
    }

#ifdef __OSCONFIG_HAS_LEGACY_PCI
    // Without ACPI we assume all 8042 devices are present
    if (!g_acpiAvailable || (acpi.BootFlags & ACPI_IA_8042) || acpi.BootFlags == 0) {
        __InstallPS2Controller();
    }
#endif

    (void)FirmwareQuery(&firmware);
    switch (firmware.Primary) {
        case OSFIRMWARE_ACPI:
            __EnumerateMcfg();
            break;
        case OSFIRMWARE_DEVICETREE:
            __EnumerateDeviceTree();
            break;
        default:
            break;
    }

#ifdef __OSCONFIG_HAS_LEGACY_PCI
    if (g_pciRoots.head == NULL) {
        __EnumerateLegacy();
    }
#endif

    if (g_pciRoots.head == NULL) {
        WARNING("BusEnumerate no usable PCI configuration hosts");
    }

    // we do not need to take a lock here
    _foreach(element, &g_pciRoots) {
        PciPublishDevice(element->value);
    }
}

unsigned int PciToDevClass(uint32_t Class, uint32_t SubClass) {
    return ((Class & 0xFFFF) << 16 | (SubClass & 0xFFFF));
}

unsigned int PciToDevSubClass(uint32_t Interface) {
    return ((Interface & 0xFFFF) << 16 | 0);
}

static oserr_t __GetPciDeviceNativeHeader(
    _In_  PciDevice_t*        parent,
    _In_  int                 bus,
    _In_  int                 slot,
    _In_  int                 function,
    _Out_ PciNativeHeader_t** headerOut)
{
    PciNativeHeader_t* nativeHeader;

    nativeHeader = (PciNativeHeader_t*)malloc(sizeof(PciNativeHeader_t));
    if (!nativeHeader) {
        return OS_EOOM;
    }

    // Read entire function information
    PciReadFunction(nativeHeader, parent->Host, (unsigned int)bus, (unsigned int)slot, (unsigned int)function);

    *headerOut = nativeHeader;
    return OS_EOK;
}

static oserr_t
PciCheckFunction(
    _In_ PciDevice_t* parent,
    _In_ int          bus,
    _In_ int          slot,
    _In_ int          function)
{
    oserr_t                     oserr;
    PciDevice_t*                device;
    int                         secondBus;
    uint16_t                    settings;
    BusDevice_t                 resources = { 0 };

    device = (PciDevice_t*)malloc(sizeof(PciDevice_t));
    if (!device) {
        return OS_EOOM;
    }

    oserr = __GetPciDeviceNativeHeader(parent, bus, slot, function, &device->Header);
    if (oserr != OS_EOK) {
        free(device);
        return oserr;
    }

    device->Handler = NULL;
    device->Attachment = NULL;
    device->Parent      = parent;
    device->Host       = parent->Host;
    device->Bus         = bus;
    device->Slot        = slot;
    device->Function    = function;
    device->AcpiConform = 0;
    device->InterruptLine = device->Header->InterruptLine;
    device->IsBridge    = DEVICE_IS_PCI_BRIDGE(device) ? 1 : 0; // this relies on device->Header
    ELEMENT_INIT(&device->list_header, 0, device);
    ELEMENT_INIT(&device->child_header, (uintptr_t)device->IsBridge, device);
    list_construct(&device->children);

    // Trace Information about device 
    // Ignore the spam of device_id 0x7a0 in VMWare
    // This is VIRTIO devices
    if (device->Header->DeviceId != 0x7a0) {
        TRACE(" - [%x:%x:%x] %s", bus, slot, function,
              PciToString(device->Header->Class, device->Header->Subclass, device->Header->Interface));
    }

    // Do some disabling, but NOT on the video or bridge
    if ((device->Header->Class != PCI_CLASS_BRIDGE)
        && (device->Header->Class != PCI_CLASS_VIDEO)) {
        uint16_t pciSettings = PciRead16(device->Host, bus, slot, function, 0x04);
        PciWrite16(device->Host, bus, slot, function, 0x04, pciSettings | PCI_COMMAND_INTDISABLE);
    }
    
    device->Handler = PciFunctionHandlerFind(device);
    if ((device->Host->DriversBlocked || device->Handler != NULL) && !device->IsBridge) {
        settings = PciRead16(device->Host, bus, slot, function, 0x04);
        settings &= ~PCI_COMMAND_BUSMASTER;
        PciWrite16(device->Host, bus, slot, function, 0x04, settings | PCI_COMMAND_INTDISABLE);
    }

    // Every consumer sees the same probe result. Quarantined functions retain
    // descriptions for diagnostics and child discovery without registering I/O.
    memset(&device->Resources, 0, sizeof(device->Resources));
    device->Resources.Firmware = device->Host->Firmware;
    resources.Bus = bus;
    resources.Slot = slot;
    resources.Function = function;
    
    if (!device->IsBridge) {
        PciProbeBars(device->Host, &resources, device->Header->HeaderType, device->Resources.Bars);
        PciDiagnoseBars(device->Host, &resources, device->Resources.Bars);
    }
    
    if (device->Handler != NULL && !device->IsBridge) {
        oserr = device->Handler->Attach(device, &device->Resources, &device->Attachment);
        if (oserr != OS_EOK) {
            WARNING("PCI %u:%u:%u.%u function attachment failed (%u)",
                device->Host->Identification.Segment, device->Bus, device->Slot, device->Function, oserr);
        }
    }

    // add device to lists
    list_append(&g_pciDevices, &device->list_header);
    list_append(&parent->children, &device->child_header);

    if (DEVICE_IS_PCI_BRIDGE(device)) {
        // Extract secondary bus
        secondBus = PciReadSecondaryBusNumber(device->Host, bus, slot, function);
        PciCheckBus(device, secondBus);
    } else {
        PciResolveInterruptLineAndPin(parent, bus, slot, function, device);
    }
    return OS_EOK;
}

void
PciCheckDevice(
    _In_ PciDevice_t* parent,
    _In_ int          bus,
    _In_ int          slot)
{
    uint16_t vendorId;
    uint8_t  headerType;
    int      function = 0;

    // Validate the vendor id, it's invalid only
    // if there is no device on that location
    vendorId = PciReadVendorId(
        parent->Host,
        (unsigned int)bus,
        (unsigned int)slot,
        (unsigned int)function
    );

    // Sanitize if device is present
    if (vendorId == 0xFFFF) {
        return;
    }

    // Check base function
    PciCheckFunction(parent, bus, slot, function);

    // Multi-function or single? 
    // If it is a multi-function device, check remaining functions
    headerType = PciReadHeaderType(
        parent->Host,
        (unsigned int)bus,
        (unsigned int)slot,
        (unsigned int)function
    );
    if (headerType & 0x80) {
        for (function = 1; function < 8; function++) {
            if (PciReadVendorId(parent->Host, bus, slot, function) != 0xFFFF) {
                PciCheckFunction(parent, bus, slot, function);
            }
        }
    }
}

void
PciCheckBus(
    _In_ PciDevice_t* parent,
    _In_ int          bus)
{
    int device;

    if (parent == NULL) {
        return;
    }

    if (bus < parent->Host->Identification.BusStart || bus > parent->Host->Identification.BusEnd) {
        return;
    }

    // Iterate all possible 32 devices on the pci-bus
    for (device = 0; device < 32; device++) {
        PciCheckDevice(parent, bus, device);
    }
}

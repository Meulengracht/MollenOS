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
#include "bcm.h"
#include <devices.h>
#include <ddk/acpi.h>
#include <ddk/busdevice.h>
#include <ddk/firmware.h>
#include <ddk/interrupt.h>
#include <ddk/utils.h>
#include <firmware/fdt.h>
#include <stdlib.h>
#include <ds/list.h>
#include <threads.h>

#define DEVICE_IS_PCI_BRIDGE(device) ((device)->Header->Class == PCI_CLASS_BRIDGE && (device)->Header->Subclass == PCI_BRIDGE_SUBCLASS_PCI)

void PciCheckBus(PciDevice_t* parent, int bus);

#ifdef __OSCONFIG_HAS_LEGACY_PCI
oserr_t __InstallPS2Controller(void);
#endif

static list_t g_pciDevices;
static mtx_t  g_pciDevicesLock;
static list_t g_pciRoots = LIST_INIT;
static int    g_acpiAvailable = 0;

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
        if ((unsigned int)device->Host->Segment == segment &&
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

static oserr_t
__PciAttachHost(
    _In_ PciHost_t*                 bus,
    _In_ struct PciFirmwareMapping* mapping)
{
    PciDevice_t* root;

    root = calloc(1, sizeof(*root));
    if (root == NULL) {
        PciHostDestroy(bus);
        return OS_EOOM;
    }
    
    list_construct(&root->children);
    ELEMENT_INIT(&root->list_header, (uintptr_t)bus->Segment, root);
    
    root->Host = bus;
    root->Bus = (unsigned int)bus->BusStart;
    root->IsBridge = 1;
    
    bus->RootDevice = root;
    bus->FirmwareMapping = mapping;
    
    if (mapping != NULL) {
        mapping->References++;
    }
    list_append(&g_pciRoots, &root->list_header);
    return OS_EOK;
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
    bus->BusEnd = 255;
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
    _In_ uint32_t segment,
    _In_ uint8_t  busStart,
    _In_ uint8_t  busEnd,
    _In_ uint64_t base,
    _In_ const struct FdtPciHost* firmwareHost,
    _In_ struct PciFirmwareMapping* mapping)
{
    PciHost_t* bus;
    size_t    length = (size_t)(busEnd - busStart + 1) << 20;

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

    bus->IsExtended     = 1;
    bus->BusStart       = busStart;
    bus->BusEnd         = busEnd;
    bus->Segment        = (int)segment;
    bus->Operations     = &g_pciAcpiEcamOperations;

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

/* PciValidateBarSize
 * Validates the size of a bar and the validity of the bar-size */
uint64_t
PciValidateBarSize(
    _In_ uint64_t base,
    _In_ uint64_t maxBase,
    _In_ uint64_t mask)
{
    uint64_t encodedSize = mask & maxBase;
    uint64_t size;

    if (!encodedSize) {
        return 0;
    }

    // BAR probing returns an address mask. Isolate its least-significant set
    // bit to obtain the actual power-of-two byte length. I/O resource lengths
    // are counts, so returning size - 1 truncates the final byte and rejects a
    // capability whose range ends exactly at the BAR boundary.
    size = encodedSize & ~(encodedSize - 1);
    if (base == maxBase && ((base | (size - 1)) & mask) != mask) {
        return 0;
    }
    return size;
}

/* PciReadBars
 * Reads and initializes all available bars for the given pci-device */
static void
__CreateBarResource(
    _In_ PciHost_t*   bus,
    _Out_ DeviceIo_t* resource,
    _In_ uint32_t    space,
    _In_ uint64_t    address,
    _In_ uint64_t    length)
{
    uint64_t physical = address;

    if (length == 0) {
        return;
    }
    
    if (bus->Operations->Translate != NULL &&
        bus->Operations->Translate(bus, space, address, length, &physical) != OS_EOK) {
        WARNING("PCI BAR is outside host windows (segment %u)", (unsigned int)bus->Segment);
        return;
    }
    
    if (length > SIZE_MAX || physical > UINTPTR_MAX || length - 1 > UINTPTR_MAX - physical) {
        return;
    }
    
    if (space == 1) {
        if (bus->IoResourcePolicy == PciIoResourcePorts) {
            if (physical > UINT16_MAX || length - 1 > UINT16_MAX - physical) {
                return;
            }
            CreateDevicePortIo(resource, (uint16_t)physical, (size_t)length);
        } else {
            CreateDeviceMemoryIo(resource, (uintptr_t)physical, (size_t)length);
        }
    } else {
        CreateDeviceMemoryIo(resource, (uintptr_t)physical, (size_t)length);
    }
}

static void
__PciReadBars(
    _In_ PciHost_t*    bus,
    _In_ BusDevice_t* device,
    _In_ uint32_t     headerType,
    _In_ int          createResources,
    _Out_ struct PciMemoryRange* memoryBars)
{
    // Buses have 2 io spaces, devices have 6
    int count = (headerType & 0x1) == 0x1 ? 2 : 6;
    int i;
    uint16_t command;

    // Size probing temporarily writes address bits. Disable decoding so those
    // transient addresses cannot redirect a real access to another resource.
    command = PciRead16(bus, device->Bus, device->Slot, device->Function, 0x04);
    PciWrite16(bus, device->Bus, device->Slot, device->Function, 0x04,
        command & ~(PCI_COMMAND_MMIO | PCI_COMMAND_PORTIO));

    /* Iterate all the avilable bars */
    for (i = 0; i < count; i++) {
        uint32_t space32, size32, mask32;
        uint64_t space64, size64, mask64;
        int      barIndex = i;
        uint32_t memorySpace = 0;
        size_t   offset = 0x10 + (i << 2);

        // Calculate the initial mask 
        mask32 = (headerType & 0x1) == 0x1 ? ~0x7FF : 0xFFFFFFFF;

        // Read both space and size
        space32 = PciRead32(bus, device->Bus, device->Slot, device->Function, offset);
        PciWrite32(bus, device->Bus, device->Slot, device->Function, offset, space32 | mask32);
        size32 = PciRead32(bus, device->Bus, device->Slot, device->Function, offset);
        PciWrite32(bus, device->Bus, device->Slot, device->Function, offset, space32);

        // Sanitize bounds of values
        if (size32 == 0xFFFFFFFF) {
            size32 = 0;
        }
        if (space32 == 0xFFFFFFFF) {
            space32 = 0;
        }

        // Which kind of io-space is it, if bit 0 is set, it's io and not mmio 
        if (space32 & 0x1) {
            // Update mask to reflect IO space
            mask64  = 0xFFFC;
            size64  = size32;
            space64 = space32 & 0xFFFC;

            // Correctly update the size of the io
            size64 = PciValidateBarSize(space64, size64, mask64);
            if (createResources && space64 != 0 && size64 != 0) {
                __CreateBarResource(bus, &device->IoSpaces[i], 1, space64, size64);
            }
        }
        // Ok, its memory, but is it 64 bit or 32 bit? 
        // Bit 2 is set for 64 bit memory space
        else if (space32 & 0x4) {
            memorySpace = 3;
            space64 = space32 & 0xFFFFFFF0;
            size64  = size32 & 0xFFFFFFF0;
            mask64  = 0xFFFFFFFFFFFFFFF0;
            
            // Calculate a new 64 bit offset
            i++;
            offset = 0x10 + (i << 2);

            // Read both space and size for 64 bit
            space32 = PciRead32(bus, device->Bus, device->Slot, device->Function, offset);
            PciWrite32(bus, device->Bus, device->Slot, device->Function, offset, 0xFFFFFFFF);
            size32 = PciRead32(bus, device->Bus, device->Slot, device->Function, offset);
            PciWrite32(bus, device->Bus, device->Slot, device->Function, offset, space32);

            // Set the upper 32 bit of the space
            space64 |= ((uint64_t)space32 << 32);
            size64  |= ((uint64_t)size32 << 32);
            // Correct the size and validate
            size64 = PciValidateBarSize(space64, size64, mask64);
            if (createResources && space64 != 0 && size64 != 0) {
                // A 64-bit BAR consumes two configuration dwords, but its BAR
                // number is the index of the lower dword. Capabilities refer
                // to that index, so keep the combined resource there while i
                // advances past the upper dword.
                __CreateBarResource(bus, &device->IoSpaces[barIndex], 3, space64, size64);
            }
        }
        else {
            memorySpace = 2;
            space64 = space32 & 0xFFFFFFF0;
            size64  = size32 & 0xFFFFFFF0;
            mask64  = 0xFFFFFFF0;

            // Correct the size and validate
            size64 = PciValidateBarSize(space64, size64, mask64);
            if (createResources && space64 != 0 && size64 != 0) {
                __CreateBarResource(bus, &device->IoSpaces[i], 2, space64, size64);
            }
        }
        if (memoryBars != NULL && memorySpace && space64 && size64 &&
            bus->Operations != NULL && bus->Operations->Translate != NULL &&
            bus->Operations->Translate(bus, memorySpace, space64, size64,
                &memoryBars[barIndex].Base) == OS_EOK) {
            memoryBars[barIndex].Length = size64;
        }
        // A nonzero probe size means the resource exists even when firmware
        // left its address at zero. Keep enumeration useful, but do not invent
        // an address or publish an unusable mapping without a PCI allocator.
        if (space64 == 0 && size64 != 0) {
            WARNING("PCI %u:%u:%u.%u BAR%u is unassigned (size 0x%llx); firmware-assigned BAR required",
                (unsigned int)bus->Segment, device->Bus, device->Slot, device->Function,
                (unsigned int)barIndex, (unsigned long long)size64);
        }
    }
    PciWrite16(bus, device->Bus, device->Slot, device->Function, 0x04, command);
}

void
PciReadBars(
    _In_ PciHost_t*    bus,
    _In_ BusDevice_t* device,
    _In_ uint32_t     headerType)
{
    __PciReadBars(bus, device, headerType, 1, NULL);
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
    struct PciFunctionResources functionResources = { 0 };

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
        
        // Quarantined hosts still need useful resource diagnostics. Share the
        // BAR probe with normal driver registration, without creating mappings
        // or loading a driver. This reports implemented but unassigned BARs.
        resources.Bus = bus;
        resources.Slot = slot;
        resources.Function = function;
        
        __PciReadBars(device->Host, &resources, device->Header->HeaderType, 0, functionResources.Bars);
        if (device->Handler != NULL) {
            functionResources.Firmware = device->Host->Firmware;
            oserr = device->Handler->Attach(device, &functionResources, &device->Attachment);
            if (oserr != OS_EOK) {
                WARNING("PCI %u:%u:%u.%u function attachment failed (%u)",
                    device->Host->Segment, device->Bus, device->Slot, device->Function, oserr);
            }
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

    if (bus < parent->Host->BusStart || bus > parent->Host->BusEnd) {
        return;
    }

    // Iterate all possible 32 devices on the pci-bus
    for (device = 0; device < 32; device++) {
        PciCheckDevice(parent, bus, device);
    }
}

#ifdef __OSCONFIG_HAS_LEGACY_PCI
oserr_t
__InstallFixedBusDevice(
    _In_ BusDevice_t* device,
    _In_ const char*  Description)
{
    uuid_t Id;

    device->Base.ParentId = UUID_INVALID;
    device->Base.Length   = sizeof(BusDevice_t);
    device->Base.VendorId = PCI_FIXED_VENDORID;

    // Set more magic constants to ignore class and subclass
    device->Base.Class    = 0xFF0F;
    device->Base.Subclass = 0xFF0F;
    device->Base.Identification.Description = strdup(Description);

    // Invalidate irqs, this must be set by fixed drivers
    device->InterruptPin         = INTERRUPT_NONE;
    device->InterruptLine        = INTERRUPT_NONE;
    device->InterruptAcpiConform = 0;
    return DmDeviceCreate(&device->Base, DEVICE_REGISTER_FLAG_LOADDRIVER, &Id);
}

oserr_t
__InstallPS2Controller(void)
{
    BusDevice_t* device;
    oserr_t      oserr;

    device = malloc(sizeof(BusDevice_t));
    if (device == NULL) {
        return OS_EOOM;
    }
    memset(device, 0, sizeof(BusDevice_t));

    // Set default ps2 device settings
    device->Base.ProductId = PCI_PS2_DEVICEID;

    // Register io-spaces for the ps2 controller, it has two ports
    // Data port - 0x60
    // oserr/Command port - 0x64
    // one byte each
    oserr = CreateDevicePortIo(&device->IoSpaces[0], 0x60, 1);
    if (oserr != OS_EOK) {
        ERROR(" > failed to initialize ps2 data io space");
        return OS_EUNKNOWN;
    }

    oserr = CreateDevicePortIo(&device->IoSpaces[1], 0x64, 1);
    if (oserr != OS_EOK) {
        ERROR(" > failed to initialize ps2 command/status io space");
        return OS_EUNKNOWN;
    }
    return __InstallFixedBusDevice(device, "PS/2 Controller");
}
#endif

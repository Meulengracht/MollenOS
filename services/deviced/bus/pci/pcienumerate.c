/**
 * MollenOS
 *
 * Copyright 2017, Philip Meulengracht
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
#include <ddk/interrupt.h>
#include <ddk/utils.h>
#include <stdlib.h>
#include <ds/list.h>
#include <threads.h>

#define DEVICE_IS_PCI_BRIDGE(device) ((device)->Header->Class == PCI_CLASS_BRIDGE && (device)->Header->Subclass == PCI_BRIDGE_SUBCLASS_PCI)

/* PCI-Express Support
 * This is the acpi-mcfg entry structure that represents an pci-express controller */
PACKED_TYPESTRUCT(McfgEntry, {
    uint64_t BaseAddress;
    uint16_t SegmentGroup;
    uint8_t  StartBus;
    uint8_t  EndBus;
    uint32_t Reserved;
});

void       PciCheckBus(PciDevice_t* parent, int bus);
oserr_t __InstallPS2Controller(void);
void       PciInstallDriverCallback(PciDevice_t* pciDevice);

static list_t       g_pciDevices;
static mtx_t        g_pciDevicesLock;
static PciDevice_t* g_rootDevice    = NULL;
static int          g_acpiAvailable = 0;

void BusEnumerate(void)
{
    ACPI_TABLE_MCFG*   mcfgTable = NULL;
    ACPI_TABLE_HEADER* header    = NULL;
    AcpiDescriptor_t   acpi      = { 0 };
    element_t*         element;
    oserr_t            osStatus;
    int                function;

    g_rootDevice = (PciDevice_t*)malloc(sizeof(PciDevice_t));
    if (!g_rootDevice) {
        return;
    }
    memset(g_rootDevice, 0, sizeof(PciDevice_t));

    // Initialize a flat list of devices
    list_construct(&g_pciDevices);
    mtx_init(&g_pciDevicesLock, mtx_plain);

    // initialize the root device
    list_construct(&g_rootDevice->children);
    g_rootDevice->IsBridge = 1;

    // Are we on an acpi-capable system?
    if (AcpiQueryStatus(&acpi) == OS_EOK) {
        TRACE("ACPI-Version: 0x%x (BootFlags 0x%x)",
              acpi.Version, acpi.BootFlags);
        g_acpiAvailable = 1;

        // Uh, even better, do we have PCI-e controllers?
        if (AcpiQueryTable(ACPI_SIG_MCFG, &header) == OS_EOK) {
            TRACE("PCI-Express Controller (mcfg length 0x%x)", header->Length);
            //McfgTable = (ACPI_TABLE_MCFG*)Header;
            //remember to free(McfgTable)
            free(header);
        }

        // Check for PS2 controller presence
        if ((acpi.BootFlags & ACPI_IA_8042) || acpi.BootFlags == 0) {
            __InstallPS2Controller();
        }
    }
    else {
        // We can pretty much assume all 8042 devices
        // are present in system, like PS2, etc
        __InstallPS2Controller();
    }

    // @todo lookup mcfg table

    // If the mcfg table is present we have pci-e controllers onboard.
    if (mcfgTable != NULL) {
        McfgEntry_t* mcfgEntry = (McfgEntry_t*)((uint8_t*)mcfgTable + sizeof(ACPI_TABLE_MCFG));
        int          entryCount = (mcfgTable->Header.Length - sizeof(ACPI_TABLE_MCFG)) / sizeof(McfgEntry_t);
        int          i;

        for (i = 0; i < entryCount; i++) {
            PciBus_t* bus = (PciBus_t*)malloc(sizeof(PciBus_t));
            size_t length;

            if (!bus) {
                return;
            }

            memset(bus, 0, sizeof(PciBus_t));

            length = (mcfgEntry->EndBus - mcfgEntry->StartBus + 1) << 20;
            if (CreateDeviceMemoryIo(&bus->IoSpace, (uintptr_t)mcfgEntry->BaseAddress, length) != OS_EOK) {
                ERROR(" > failed to create pcie address space");
                return;
            }

            // PCIe memory spaces spand up to 256 mb
            bus->IsExtended     = 1;
            bus->BusStart       = mcfgEntry->StartBus;
            bus->BusEnd         = mcfgEntry->EndBus;
            bus->Segment        = mcfgEntry->SegmentGroup;
            g_rootDevice->BusIo = bus;

            for (function = bus->BusStart; function <= bus->BusEnd; function++) {
                PciCheckBus(g_rootDevice, function);
            }
            mcfgEntry++;
        }
        free(mcfgTable);
    }
    else {
        // Otherwise we have traditional PCI buses
        PciBus_t* bus = (PciBus_t*)malloc(sizeof(PciBus_t));
        if (!bus) {
            return;
        }
        memset(bus, 0, sizeof(PciBus_t));

        g_rootDevice->BusIo = bus;
        bus->BusEnd         = 7;

        // PCI buses use io
        osStatus = CreateDevicePortIo(&bus->IoSpace, PCI_IO_BASE, PCI_IO_LENGTH);
        if (osStatus != OS_EOK) {
            ERROR(" > failed to initialize pci io space");
            return;
        }

        osStatus = AcquireDeviceIo(&bus->IoSpace);
        if (osStatus != OS_EOK) {
            ERROR(" > failed to acquire pci io space");
            return;
        }

        // We can check whether or not it's a multi-function
        // root-bridge, in that case there are multiple buses
        if (!(PciReadHeaderType(bus, 0, 0, 0) & 0x80)) {
            PciCheckBus(g_rootDevice, 0);
        }
        else {
            for (function = 0; function < 8; function++) {
                if (PciReadVendorId(bus, 0, 0, function) != 0xFFFF)
                    break;
                PciCheckBus(g_rootDevice, function);
            }
        }
    }

    // we do not need to take a lock here
    _foreach(element, &g_rootDevice->children) {
        PciInstallDriverCallback(element->value);
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
    uint64_t size = mask & maxBase;

    if (!size) {
        return 0;
    }
    size = (size & ~(size - 1)) - 1;

    if (base == maxBase && ((base | size) & mask) != mask) {
        return 0;
    }
    else {
        return size;
    }
}

/* PciReadBars
 * Reads and initializes all available bars for the given pci-device */
void
PciReadBars(
    _In_ PciBus_t*    bus,
    _In_ BusDevice_t* device,
    _In_ uint32_t     headerType)
{
    // Buses have 2 io spaces, devices have 6
    int count = (headerType & 0x1) == 0x1 ? 2 : 6;
    int i;

    /* Iterate all the avilable bars */
    for (i = 0; i < count; i++) {
        uint32_t space32, size32, mask32;
        uint64_t space64, size64, mask64;
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
            if (space64 != 0 && size64 != 0) {
                CreateDevicePortIo(&device->IoSpaces[i], LOWORD(space64), (size_t)size64);
            }
        }
        // Ok, its memory, but is it 64 bit or 32 bit? 
        // Bit 2 is set for 64 bit memory space
        else if (space32 & 0x4) {
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
#if defined(i386) || defined(__i386__)
            if (sizeof(uintptr_t) < 8 && space64 > SIZE_MAX) {
                WARNING("Found 64 bit device with 64 bit address, can't use it in 32 bit mode");
                return;
            }
#endif

            // Correct the size and validate
            size64 = PciValidateBarSize(space64, size64, mask64);
            if (space64 != 0 && size64 != 0) {
                CreateDeviceMemoryIo(&device->IoSpaces[i], (uintptr_t)space64, (size_t)size64);
            }
        }
        else {
            space64 = space32 & 0xFFFFFFF0;
            size64  = size32 & 0xFFFFFFF0;
            mask64  = 0xFFFFFFF0;

            // Correct the size and validate
            size64 = PciValidateBarSize(space64, size64, mask64);
            if (space64 != 0 && size64 != 0) {
                CreateDeviceMemoryIo(&device->IoSpaces[i], (uintptr_t)space64, (size_t)size64);
            }
        }
    }
}

static inline void __UpdateInterruptLine(
        _In_ PciDevice_t* parent,
        _In_ int          bus,
        _In_ int          slot,
        _In_ int          function,
        _In_ int          interruptLine,
        _In_ PciDevice_t* pciDevice)
{
    PciWrite8(parent->BusIo, (unsigned int)bus, (unsigned int)slot,
              (unsigned int)function, 0x3C, (uint8_t)interruptLine);
    pciDevice->Header->InterruptLine = (uint8_t)interruptLine;
}

static inline int __SwizzleInterruptPin(int device, int pin) {
    return (((pin - 1) + device) % 4) + 1;
}

static void __ResolveInterruptLineAndPin(
        _In_ PciDevice_t* parent,
        _In_ int          bus,
        _In_ int          slot,
        _In_ int          function,
        _In_ PciDevice_t* pciDevice)
{
    TRACE("__ResolveInterruptLineAndPin(bus=%i, slot=%i, function=%i)",
          bus, slot, function);

    // We do need acpi for this to query acpi interrupt information for device
    if (g_acpiAvailable == 1) {
        PciDevice_t* iterator      = pciDevice;
        unsigned int acpiConform   = 0;
        int          interruptLine = pciDevice->Header->InterruptLine;
        int          interruptPin  = pciDevice->Header->InterruptPin;
        oserr_t   hasRouting    = OS_ENOENT;
        TRACE("__ResolveInterruptLineAndPin initial line=%i, pin=%i", interruptLine, interruptPin);

        // Sanitize legals
        if (interruptPin > 4) {
            interruptPin = 1;
        }

        // Does device even use interrupts?
        if (interruptPin != 0) {
            // Swizzle till we reach root
            // Case 1 - Query device for ACPI filter
            //        -> 1.1: It has an routing for our Dev/Pin
            //             -> Exit
            //          -> 1.2: It does not have an routing
            //           -> Swizzle-pin
            //           -> Get parent device
            //           -> Go-To 1
            while (iterator && iterator != g_rootDevice) {
                hasRouting = AcpiQueryInterrupt(
                        iterator->Bus, iterator->Slot, interruptPin,
                        &interruptLine, &acpiConform);

                // Did routing exist?
                if (hasRouting == OS_EOK) {
                    break;
                }

                // Nope, swizzle pin, move up the ladder
                interruptPin = __SwizzleInterruptPin((int) iterator->Slot, interruptPin);
                iterator     = iterator->Parent;
                TRACE("__ResolveInterruptLineAndPin derived pin %i", interruptPin);
            }

            // Update the irq-line if we found a new line
            if (hasRouting == OS_EOK) {
                TRACE("__ResolveInterruptLineAndPin updating device, line=%i, pin=%i", interruptLine, interruptPin);
                __UpdateInterruptLine(parent, bus, slot, function, interruptLine, pciDevice);
                pciDevice->AcpiConform = acpiConform;
            }
        }
    }
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
    PciReadFunction(nativeHeader, parent->BusIo, (unsigned int)bus, (unsigned int)slot, (unsigned int)function);

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
    oserr_t   osStatus;
    PciDevice_t* device;
    int          secondBus;

    device = (PciDevice_t*)malloc(sizeof(PciDevice_t));
    if (!device) {
        return OS_EOOM;
    }

    osStatus = __GetPciDeviceNativeHeader(parent, bus, slot, function, &device->Header);
    if (osStatus != OS_EOK) {
        free(device);
        return osStatus;
    }

    device->Parent      = parent;
    device->BusIo       = parent->BusIo;
    device->Bus         = bus;
    device->Slot        = slot;
    device->Function    = function;
    device->AcpiConform = 0;
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
        uint16_t pciSettings = PciRead16(device->BusIo, bus, slot, function, 0x04);
        PciWrite16(device->BusIo, bus, slot, function, 0x04, pciSettings | PCI_COMMAND_INTDISABLE);
    }

    // add device to lists
    list_append(&g_pciDevices, &device->list_header);
    list_append(&parent->children, &device->child_header);

    if (DEVICE_IS_PCI_BRIDGE(device)) {
        // Extract secondary bus
        secondBus = PciReadSecondaryBusNumber(device->BusIo, bus, slot, function);
        PciCheckBus(device, secondBus);
    }
    else {
        __ResolveInterruptLineAndPin(parent, bus, slot, function, device);
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
    int      function = 0;

    // Validate the vendor id, it's invalid only
    // if there is no device on that location
    vendorId = PciReadVendorId(parent->BusIo,
                               (unsigned int)bus, (unsigned int)slot, (unsigned int)function);

    // Sanitize if device is present
    if (vendorId == 0xFFFF) {
        return;
    }

    // Check base function
    PciCheckFunction(parent, bus, slot, function);

    // Multi-function or single? 
    // If it is a multi-function device, check remaining functions
    if (PciReadHeaderType(parent->BusIo,
                          (unsigned int)bus, (unsigned int)slot, (unsigned int)function) & 0x80) {
        for (function = 1; function < 8; function++) {
            if (PciReadVendorId(parent->BusIo, bus, slot, function) != 0xFFFF) {
                PciCheckFunction(parent, bus, slot, function);
            }
        }
    }
}

/* PciCheckBus
 * Enumerates all possible devices on the given bus */
void
PciCheckBus(
    _In_ PciDevice_t* parent,
    _In_ int          bus)
{
    int device;

    // Sanitize parameters
    if (parent == NULL || bus < 0) {
        return;
    }

    // Iterate all possible 32 devices on the pci-bus
    for (device = 0; device < 32; device++) {
        PciCheckDevice(parent, bus, device);
    }
}

/* CreateBusDeviceFromPciDevice
 * Creates a new Device_t from a pci-device and registers it with the device-manager */
static oserr_t
CreateBusDeviceFromPciDevice(
    _In_ PciDevice_t* pciDevice)
{
    BusDevice_t* device;
    uuid_t       id;

    device = malloc(sizeof(BusDevice_t));
    if (device == NULL) {
        return OS_EOOM;
    }

    memset(device, 0, sizeof(BusDevice_t));
    device->Base.Id     = UUID_INVALID;
    device->Base.ParentId  = UUID_INVALID;
    device->Base.Length = sizeof(BusDevice_t);

    device->Base.VendorId  = pciDevice->Header->VendorId;
    device->Base.ProductId = pciDevice->Header->DeviceId;
    device->Base.Class     = PciToDevClass(pciDevice->Header->Class, pciDevice->Header->Subclass);
    device->Base.Subclass  = PciToDevSubClass(pciDevice->Header->Interface);
    device->Base.Identification.Description = strdup(PciToString(
            pciDevice->Header->Class,
            pciDevice->Header->Subclass,
            pciDevice->Header->Interface));

    device->Segment  = (unsigned int)pciDevice->BusIo->Segment;
    device->Bus      = pciDevice->Bus;
    device->Slot     = pciDevice->Slot;
    device->Function = pciDevice->Function;

    device->InterruptLine        = (int)pciDevice->Header->InterruptLine;
    device->InterruptPin         = (int)pciDevice->Header->InterruptPin;
    device->InterruptAcpiConform = pciDevice->AcpiConform;

    // Handle bars attached to device
    PciReadBars(pciDevice->BusIo, device, pciDevice->Header->HeaderType);

    // PCI - IDE Bar Fixup
    // From experience ide-bars don't always show up (ex: Oracle VM and Bochs)
    // but only the initial 4 bars don't, the BM bar
    // always seem to show up 
    if (pciDevice->Header->Class == PCI_CLASS_STORAGE
        && pciDevice->Header->Subclass == PCI_STORAGE_SUBCLASS_IDE) {
        if ((pciDevice->Header->Interface & 0x1) == 0) {
            if (device->IoSpaces[0].Type == DeviceIoInvalid) {
                CreateDevicePortIo(&device->IoSpaces[0], 0x1F0, 8);
            }
            if (device->IoSpaces[1].Type == DeviceIoInvalid) {
                CreateDevicePortIo(&device->IoSpaces[1], 0x3F6, 4);
            }
        }
        if ((pciDevice->Header->Interface & 0x4) == 0) {
            if (device->IoSpaces[2].Type == DeviceIoInvalid) {
                CreateDevicePortIo(&device->IoSpaces[2], 0x170, 8);
            }
            if (device->IoSpaces[3].Type == DeviceIoInvalid) {
                CreateDevicePortIo(&device->IoSpaces[3], 0x376, 4);
            }
        }
    }
    return DmDeviceCreate(
            &device->Base,
            DEVICE_REGISTER_FLAG_LOADDRIVER,
            &id
    );
}

void
PciInstallDriverCallback(
    _In_ PciDevice_t* pciDevice)
{
    // Bridge or device? 
    // If a bridge, we keep iterating, device, load driver
    if (pciDevice->IsBridge) {
        foreach(element, &pciDevice->children) {
            PciInstallDriverCallback(element->value);
        }
    }
    else {
        CreateBusDeviceFromPciDevice(pciDevice);
    }
}

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

oserr_t
DMBusControl(
        _In_ BusDevice_t*              device,
        _In_ struct OSIOCtlBusControl* request)
{
    PciDevice_t* pciDevice = NULL;
    uint16_t     settings;

    // Lookup pci-device
    foreach(element, &g_pciDevices) {
        PciDevice_t* entry = (PciDevice_t*)element->value;
        if (entry->Bus == device->Bus
            && entry->Slot == device->Slot
            && entry->Function == device->Function) {
            pciDevice = entry;
            break;
        }
    }

    // Sanitize
    if (pciDevice == NULL) {
        ERROR(" > failed to locate pci-device for ioctl");
        return OS_ENOENT;
    }

    // Read value, modify and write back
    settings = PciRead16(pciDevice->BusIo, device->Bus, device->Slot, device->Function, 0x04);

    // Clear all possible flags first
    settings &= ~(PCI_COMMAND_BUSMASTER | PCI_COMMAND_FASTBTB
                  | PCI_COMMAND_MMIO | PCI_COMMAND_PORTIO | PCI_COMMAND_INTDISABLE);

    // Handle enable
    if (!(request->Flags & __DEVICEMANAGER_IOCTL_ENABLE)) {
        settings |= PCI_COMMAND_INTDISABLE;
    }

    // Handle io/mmio
    if (request->Flags & __DEVICEMANAGER_IOCTL_MMIO_ENABLE) {
        settings |= PCI_COMMAND_MMIO;
    }
    if (request->Flags & __DEVICEMANAGER_IOCTL_IO_ENABLE) {
        settings |= PCI_COMMAND_PORTIO;
    }

    // Handle busmaster
    if (request->Flags & __DEVICEMANAGER_IOCTL_BUSMASTER_ENABLE) {
        settings |= PCI_COMMAND_BUSMASTER;
    }

    // Handle fast-b2b
    if (request->Flags & __DEVICEMANAGER_IOCTL_FASTBTB_ENABLE) {
        settings |= PCI_COMMAND_FASTBTB;
    }

    // Handle memory write and invalidate
    if (request->Flags & __DEVICEMANAGER_IOCTL_MEMWRTINVD_ENABLE) {
        settings |= PCI_COMMAND_MEMWRITE;
    }

    // Write back settings
    PciWrite16(pciDevice->BusIo, device->Bus, device->Slot, device->Function, 0x04, settings);
    return OS_EOK;
}

oserr_t
DmIoctlDeviceEx(
	_In_ BusDevice_t* device,
	_In_ int          direction,
	_In_ unsigned int Register,
	_In_ size_t*      value,
	_In_ size_t       width)
{
    PciDevice_t* pciDevice = NULL;

    // Lookup pci-device
    foreach(element, &g_pciDevices) {
        PciDevice_t* entry = (PciDevice_t*)element->value;
        if (entry->Bus == device->Bus
            && entry->Slot == device->Slot
            && entry->Function == device->Function) {
            pciDevice = entry;
            break;
        }
    }

    if (pciDevice == NULL) {
        ERROR(" > failed to locate pci-device for ioctl");
        return OS_ENOENT;
    }

    if (direction == __DEVICEMANAGER_IOCTL_EXT_READ) {
        if (width == 1) {
            *value = (size_t)PciRead8(pciDevice->BusIo, device->Bus,
                                      device->Slot, device->Function, Register);
        }
        else if (width == 2) {
            *value = (size_t)PciRead16(pciDevice->BusIo, device->Bus,
                                       device->Slot, device->Function, Register);
        }
        else if (width == 4) {
            *value = (size_t)PciRead32(pciDevice->BusIo, device->Bus,
                                       device->Slot, device->Function, Register);
        }
        else {
            return OS_EINVALPARAMS;
        }
    }
    else {
        if (width == 1) {
            PciWrite8(pciDevice->BusIo, device->Bus,
                      device->Slot, device->Function, Register, LOBYTE(*value));
        }
        else if (width == 2) {
            PciWrite16(pciDevice->BusIo, device->Bus,
                       device->Slot, device->Function, Register, LOWORD(*value));
        }
        else if (width == 4) {
            PciWrite32(pciDevice->BusIo, device->Bus,
                       device->Slot, device->Function, Register, LODWORD(*value));
        }
        else {
            return OS_EINVALPARAMS;
        }
    }
    return OS_EOK;
}

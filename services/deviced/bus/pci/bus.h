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
 * MollenOS PCI Bus Driver 
 * - Enumerates the bus and registers the devices/controllers
 *   available in the system
 */

#ifndef __PCI_BUS_INTERFACE__
#define __PCI_BUS_INTERFACE__

#include <os/osdefs.h>
#include <ddk/io.h>
#include <ds/list.h>
#include <core/publication.h>
#include "host.h"

#include "bars.h"

// Forward declarations
typedef struct PciHost PciHost_t;
struct BusDevice;
struct PciDevice;
struct PciFirmwareMapping;
struct FdtPciHost;

/* Fixed device-id and vendor-id values for 
 * loading non-dynamic devices */
#define PCI_FIXED_VENDORID              0xFFEF
#define PCI_CMOS_RTC_DEVICEID           0x0010
#define PCI_PIT_DEVICEID                0x0020
#define PCI_PS2_DEVICEID                0x0030

/* To be able to access bus data we need io-space
 * access, so lets define the io-ports neccessary
 * for accessing PCI (legacy), not PCIe */
#define PCI_IO_BASE                     0xCF8
#define PCI_IO_LENGTH                   8
#define PCI_REGISTER_SELECT             0x00
#define PCI_REGISTER_DATA               0x04

/* Pci Type definitions, helps us figure out what
 * kind of device/controller we are dealing with */
#define PCI_CLASS_NONE                  0x00
#define PCI_CLASS_STORAGE               0x01
#define PCI_CLASS_NETWORK               0x02
#define PCI_CLASS_VIDEO                 0x03
#define PCI_CLASS_MULTIMEDIA            0x04
#define PCI_CLASS_MEMORY                0x05
#define PCI_CLASS_BRIDGE                0x06
#define PCI_CLASS_COMMUNICATION         0x07
#define PCI_CLASS_PERIPHERAL            0x08
#define PCI_CLASS_INPUT                 0x09
#define PCI_CLASS_DOCKING               0x0A
#define PCI_CLASS_PROCESSORS            0x0B
#define PCI_CLASS_SERIAL                0x0C
#define PCI_CLASS_WIRELESS              0x0D
#define PCI_CLASS_IOCONTROLLER          0x0E
#define PCI_CLASS_SATELLITE             0x0F
#define PCI_CLASS_ENCRYPTION            0x10
#define PCI_CLASS_SIGNALDATA            0x11

#define PCI_STORAGE_SUBCLASS_IDE        0x01

#define PCI_BRIDGE_SUBCLASS_PCI         0x04

/* The different command flag bits that can be 
 * manipulated in Pci base entry (Command) field */
#define PCI_COMMAND_PORTIO              0x1
#define PCI_COMMAND_MMIO                0x2
#define PCI_COMMAND_BUSMASTER           0x4
#define PCI_COMMAND_SPECIALCYC          0x8
#define PCI_COMMAND_MEMWRITE            0x10
#define PCI_COMMAND_VGAPALET            0x20
#define PCI_COMMAND_PARRITYERR          0x40
#define PCI_COMMAND_SERRENABLE          0x100
#define PCI_COMMAND_FASTBTB             0x200
#define PCI_COMMAND_INTDISABLE          0x400

/* The PCI base entry on the pci-databus
 * It describes a device on the pci-bus, the resources
 * its command register, status and its system bars */
PACKED_TYPESTRUCT(PciNativeHeader, {
    uint16_t VendorId; /* 0x00 */
    uint16_t DeviceId; /* 0x02 */
    uint16_t Command;  /* 0x04 */
    uint16_t Status;   /* 0x06 */
    uint8_t  Revision; /* 0x08 */
    uint8_t  Interface;/* 0x09 */
    uint8_t  Subclass; /* 0x0A */
    uint8_t  Class;    /* 0x0B */
    uint8_t  CacheLineSize;/* 0x0C */
    uint8_t  LatencyTimer; /* 0x0D */
    uint8_t  HeaderType;   /* 0x0E */
    uint8_t  Bist;     /* 0x0F */
    uint32_t Bar0;     /* 0x10 */
    uint32_t Bar1;     /* 0x14 */
    uint32_t Bar2;     /* 0x18 */
    uint32_t Bar3;     /* 0x1C */
    uint32_t Bar4;     /* 0x20 */
    uint32_t Bar5;     /* 0x24 */
    uint32_t CardbusCISPtr;/* 0x28 */
    uint16_t SubSystemVendorId;/* 0x2C */
    uint16_t SubSystemId;  /* 0x2E */
    uint32_t ExpansionRomBaseAddr;/* 0x30 */
    uint32_t Reserved0;    /* 0x34 */
    uint32_t Reserved1;    /* 0x38 */
    uint8_t  InterruptLine;/* 0x3C */
    uint8_t  InterruptPin; /* 0x3D */
    uint8_t  MinGrant;     /* 0x3E */
    uint8_t  MaxLatency;   /* 0x3F */
});

struct PciFunctionResources {
    struct PciBar            Bars[6];
    const struct FdtPciHost* Firmware;
};

/**
 * @brief Handles a PCI function that has child devices of its own.
 * A match keeps a generic driver from claiming the function, even if Attach fails.
 * When BlockActivation is set, requests to change the function's PCI settings are
 * rejected. Attach must leave its output NULL and free partial state on failure;
 * on success, PCI owns the attachment. Copy resources that must outlive Attach.
 * Attach records child devices; Publish adds them after scanning, without starting
 * their drivers. Destroy runs after the publication group is removed and before
 * host resources are released.
 */
struct PciFunctionHandler {
    int BlockActivation;

    /**
     * @brief Determine if a function handler exists for this PCI device.
     * 
     * @param device The PCI device to check for a match.
     * @return Non-zero if the handler matches the device, zero otherwise.
     */
    int (*Match)(
        const struct PciDevice* device);
    
    /**
     * @brief Attach a function handler to the given PCI device.
     * 
     * @param device The PCI device to attach to.
     * @param resources The resources allocated for the function.
     * @param attachmentOut Output parameter for the attachment.
     * @return An error code indicating the result of the attachment.
     */
    oserr_t (*Attach)(
        const struct PciDevice*            device,
        const struct PciFunctionResources* resources, 
        void**                             attachmentOut);
    
    /**
     * @brief Destroy the attachment associated with the function handler.
     * 
     * @param attachment The attachment to destroy.
     */
    void (*Destroy)(void* attachment);

    /**
     * @brief Adds child descriptions to the host's group beneath this function.
     * The group handles driver matching and removal, including partial failure.
     *
     * @param attachment The attachment associated with the function handler.
     * @param device The already registered parent PCI function.
     * @param group The host's group, still accepting descriptions.
     * @return OS_EOK on success, or the first error adding a child.
     */
    oserr_t (*Publish)(
        void*                      attachment,
        const struct PciDevice*    device,
        struct DmPublicationGroup* group);
};

/** @brief Returns the first statically registered handler matching a function. */
extern const struct PciFunctionHandler*
PciFunctionHandlerFind(
    _In_ const struct PciDevice* device);

struct PciHostOperations {
    /**
     * @brief Read from the PCI configuration space.
     * 
     * @param host The PCI host to read from.
     * @param bus The bus number of the target device.
     * @param slot The slot number of the target device.
     * @param function The function number of the target device.
     * @param reg The offset within the configuration space.
     * @param width The size of the read operation.
     * @return The value read from the configuration space.
     */
    size_t (*Read)(
        struct PciHost* host,
        unsigned int    bus,
        unsigned int    slot,
        unsigned int    function,
        size_t          reg,
        size_t          width);
    
    /**
     * @brief Write to the PCI configuration space.
     * 
     * @param host The PCI host to write to.
     * @param bus The bus number of the target device.
     * @param slot The slot number of the target device.
     * @param function The function number of the target device.
     * @param reg The offset within the configuration space.
     * @param width The size of the write operation.
     * @param value The value to write.
     */
    void (*Write)(
        struct PciHost* host,
        unsigned int    bus,
        unsigned int    slot,
        unsigned int    function,
        size_t          reg,
        size_t          value,
        size_t          width);

    /**
    * @brief Releases state used only by this host implementation. Common teardown
    * releases the main I/O mapping, retained firmware data, and host allocation.
     * @param host The PCI host to destroy.
     */
    void (*Destroy)(struct PciHost*);
    
    /**
     * @brief Translate a PCI address to a physical address.
     * 
     * @param host The PCI host to use for translation.
     * @param space The address space.
     * @param address The PCI address to translate.
     * @param length The length of the address range.
     * @param physicalOut The output physical address.
     * @return An error code indicating success or failure.
     */
    oserr_t (*Translate)(
        struct PciHost* host,
        uint32_t        space,
        uint64_t        address,
        uint64_t        length,
        uint64_t*       physicalOut);
    
    /**
     * @brief Resolve the interrupt for a PCI device.
     * 
     * @param host The PCI host to use for resolution.
     * @param bus The bus number of the target device.
     * @param slot The slot number of the target device.
     * @param function The function number of the target device.
     * @param pin The interrupt pin of the target device.
     * @param lineOut The output interrupt line.
     * @param flagsOut The output interrupt flags.
     * @return An error code indicating success or failure.
     */
    oserr_t (*ResolveInterrupt)(
        struct PciHost* host,
        unsigned int    bus,
        unsigned int    slot,
        unsigned int    function,
        unsigned int    pin,
        int*            lineOut,
        unsigned int*   flagsOut);
};

/**
 * @brief How a host exposes PCI I/O BAR resources to device drivers.
 */
enum PciIoResourcePolicy {
    PciIoResourcePorts,
    PciIoResourceMemory
};

/**
 * @brief Represents one PCI controller, its segment and bus range, and any
 * firmware data used to describe its devices. Configuration access is provided
 * separately from the way device I/O resources are exposed to drivers.
 */
typedef struct PciHost {
    DeviceIo_t               IoSpace;
    enum PciIoResourcePolicy IoResourcePolicy;
    int                      IsExtended;
    struct PciHostIdentification Identification;
    
    const struct PciHostOperations* Operations;
    void*                           OpContext;

    const struct FdtPciHost*   Firmware;
    int                        DriversBlocked;
    uint8_t                    ScannedBuses[32];
    struct PciDevice*          RootDevice;
    // Includes PCI descriptions and every attached bus, such as RP1.
    struct DmPublicationGroup  Publication;
    struct PciFirmwareMapping* FirmwareMapping;
} PciHost_t;

/**
 * @brief Represents a device on the pci-bus, keeps information
 * about location, children, and a parent device/controller
 */
typedef struct PciDevice {
    element_t         list_header;
    element_t         child_header;
    struct PciDevice* Parent;
    PciHost_t*        Host;
    int               IsBridge;
    // Registry IDs are distinct from host identity and PCI addresses.
    uuid_t            DeviceId;
    // Changed under the PCI lock; nonzero references prevent host destruction.
    unsigned int      ProviderReferences;
    DeviceIo_t        PublishedIo[6];

    struct PciFunctionResources      Resources;
    const struct PciFunctionHandler* Handler;
    void*                            Attachment;

    unsigned int Bus;
    unsigned int Slot;
    unsigned int Function;
    unsigned int AcpiConform;
    int          InterruptLine;

    PciNativeHeader_t* Header;
    list_t             children;
} PciDevice_t;

/**
 * @brief Finds PCI controllers described by firmware and scans their buses.
 * On supported systems, scans legacy PCI if no firmware-described host starts.
 */
__EXTERN void
BusEnumerate(void);

/**
 * @brief Releases a host after its clients have stopped.
 * This also frees a newly constructed host if registration failed, even if it
 * has no root device. The host's controller state, I/O mapping, and firmware data
 * are released; other hosts are unaffected. If removing device-manager entries
 * fails, the host and remaining IDs stay valid so the caller can retry.
 */
__EXTERN oserr_t
PciHostDestroy(
    _In_ PciHost_t* bus);

/**
 * @brief Reads a 32 bit value from the pci-bus at the specified location bus, slot, function and register.
 */
__EXTERN uint32_t
PciRead32(
    _In_ PciHost_t*    Io,
    _In_ unsigned int Bus, 
    _In_ unsigned int Slot, 
    _In_ unsigned int Function, 
    _In_ size_t       Register);

/**
 * @brief Reads a 8/16 bit value from the pci-bus at the specified location bus, device, function and register.
 */
__EXTERN uint16_t PciRead16(PciHost_t *Io, unsigned int Bus, unsigned int Device, unsigned int Function, size_t Register);
__EXTERN uint8_t PciRead8(PciHost_t *Io, unsigned int Bus, unsigned int Device, unsigned int Function, size_t Register);

/**
 * @brief Writes a 8/16/32 bit value to the pci-bus at the specified location bus, device, function and register.
 */
__EXTERN void PciWrite32(PciHost_t *Io, unsigned int Bus, unsigned int Device, unsigned int Function, size_t Register, uint32_t Value);
__EXTERN void PciWrite16(PciHost_t *Io, unsigned int Bus, unsigned int Device, unsigned int Function, size_t Register, uint16_t Value);
__EXTERN void PciWrite8(PciHost_t *Io, unsigned int Bus, unsigned int Device, unsigned int Function, size_t Register, uint8_t Value);

/**
 * @brief Read or writes a value of the given length from the given register of the specified PCI device.
 */
__EXTERN uint32_t PciDeviceRead(PciDevice_t *Device, size_t Register, size_t Length);
__EXTERN void PciDeviceWrite(PciDevice_t *Device, size_t Register, uint32_t Value, size_t Length);

/**
 * @brief Writes a value of the given length to the given register of the specified PCI device.
 */

/**
 * @brief Reads the vendor id at given bus/device/function location.
 */
__EXTERN uint16_t PciReadVendorId(PciHost_t *Host, 
    unsigned int Bus, unsigned int Device, unsigned int Function);

/**
 * @brief Reads in the pci header that exists at the given location and fills out the information into <Pcs>.
 */
__EXTERN void PciReadFunction(PciNativeHeader_t *Pcs,
    PciHost_t *Host, unsigned int Bus, unsigned int Device, unsigned int Function);

/**
 * @brief Reads the secondary bus number at given pci device location. This can be used to get the bus-number behind a bridge.
 */
__EXTERN uint8_t PciReadSecondaryBusNumber(PciHost_t *Host, 
    unsigned int Bus, unsigned int Device, unsigned int Function);

/**
 * @brief Reads the sub class at given location.
 * Bit 7 - MultiFunction, Lower 4 bits is type.
 * Type 0 is standard, Type 1 is PCI-PCI Bridge,
 * Type 2 is CardBus Bridge.
 */
__EXTERN uint8_t PciReadHeaderType(PciHost_t *Host,
    unsigned int Bus, unsigned int Device, unsigned int Function);

/**
 * @brief Resolves the interrupt line and pin for the specified PCI device.
 * 
 * @param parent The parent PCI device or bridge.
 * @param bus The bus number of the PCI device.
 * @param slot The slot number of the PCI device.
 * @param function The function number of the PCI device.
 * @param pciDevice The PCI device for which to resolve the interrupt line and pin.
 */
__EXTERN void
PciResolveInterruptLineAndPin(
    _In_ PciDevice_t* parent,
    _In_ int          bus,
    _In_ int          slot,
    _In_ int          function,
    _In_ PciDevice_t* pciDevice);

/**
 * @brief Adds the host root and all PCI and attached child descriptions before
 * allowing drivers to match them. Adding an entry may fail; the partial group
 * is then removed. Binding failures retain the complete group for retry.
 *
 * @param pciDevice Host root to publish. Child devices are added by this call.
 * @return OS_EOK on success, or an error adding, binding, or removing entries.
 */
__EXTERN oserr_t
PciPublishDevice(
    _In_ PciDevice_t* pciDevice);

/**
 * @brief Converts the given class, subclass and interface into descriptive string to give the pci-entry a description.
 */
__EXTERN const char*
PciToString(
    _In_ uint8_t Class,
    _In_ uint8_t SubClass,
    _In_ uint8_t Interface);


/**
 * @brief Reports whether ACPI interrupt routing is available.
 */
__EXTERN int
PciIsAcpiAvailable(void);

/**
 * @brief Finds a function while the caller holds the PCI critical section.
 */
__EXTERN PciDevice_t*
PciFindDevice(
    _In_ unsigned int segment,
    _In_ unsigned int bus,
    _In_ unsigned int slot,
    _In_ unsigned int function);

/**
 * @brief Converts PCI class codes to device-manager identifiers.
 */
__EXTERN unsigned int PciToDevClass(uint32_t Class, uint32_t SubClass);
__EXTERN unsigned int PciToDevSubClass(uint32_t Interface);

/**
 * @brief Reads and publishes a function's BAR resources.
 */
__EXTERN void
PciReadBars(
    _In_ PciHost_t*        bus,
    _In_ struct BusDevice* device,
    _In_ uint32_t          headerType);

__EXTERN void 
PciCriticalSectionEnter(void);

__EXTERN void 
PciCriticalSectionLeave(void);

#endif //!__PCI_BUS_INTERFACE__

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
 * along with this program.If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * MollenOS X86 Bus Driver 
 * - Enumerates the bus and registers the devices/controllers
 *   available in the system
 */

#ifndef __X86_BUS_INTERFACE__
#define __X86_BUS_INTERFACE__

#include <os/osdefs.h>
#include <ddk/io.h>
#include <ds/list.h>

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

/* The PCI bus header, this is used
 * by the bus code, and is not related to any hardware structure. 
 * This keeps track of the bus's in the system and their io space */
typedef struct PciBus {
    DeviceIo_t IoSpace;
    int        IsExtended;
    int        Segment;
    int        BusStart;
    int        BusEnd;
} PciBus_t;

/* PCI Device header
 * Represents a device on the pci-bus, keeps information
 * about location, children, and a parent device/controller */
typedef struct PciDevice {
    element_t         list_header;
    element_t         child_header;
    struct PciDevice* Parent;
    PciBus_t*         BusIo;
    int               IsBridge;

    unsigned int Bus;
    unsigned int Slot;
    unsigned int Function;
    unsigned int AcpiConform;

    PciNativeHeader_t*  Header;
    list_t              children;
} PciDevice_t;

/* BusEnumerate
 * Enumerates the pci-bus, on newer pcs its possbile for 
 * devices exists on TWO different busses. PCI and PCI Express. */
__EXTERN int
BusEnumerate(void*);

/* PciRead32
 * Reads a 32 bit value from the pci-bus
 * at the specified location bus, slot, function and register */
__EXTERN uint32_t
PciRead32(
    _In_ PciBus_t*    Io,
    _In_ unsigned int Bus, 
    _In_ unsigned int Slot, 
    _In_ unsigned int Function, 
    _In_ size_t       Register);

/* PciRead16
 * Reads a 16 bit value from the pci-bus
 * at the specified location bus, device, function and register */
__EXTERN uint16_t PciRead16(PciBus_t *Io,
    unsigned int Bus, unsigned int Device, unsigned int Function, size_t Register);

/* PciRead8
 * Reads a 8 bit value from the pci-bus
 * at the specified location bus, device, function and register */
__EXTERN uint8_t PciRead8(PciBus_t *Io,
    unsigned int Bus, unsigned int Device, unsigned int Function, size_t Register);

/* PciWrite32
 * Writes a 32 bit value to the pci-bus
 * at the specified location bus, device, function and register */
__EXTERN void PciWrite32(PciBus_t *Io,
    unsigned int Bus, unsigned int Device, unsigned int Function, size_t Register, uint32_t Value);

/* PciWrite16
 * Writes a 16 bit value to the pci-bus
 * at the specified location bus, device, function and register */
__EXTERN void PciWrite16(PciBus_t *Io,
    unsigned int Bus, unsigned int Device, unsigned int Function, size_t Register, uint16_t Value);

/* PciWrite8
 * Writes a 8 bit value to the pci-bus
 * at the specified location bus, device, function and register */
__EXTERN void PciWrite8(PciBus_t *Io,
    unsigned int Bus, unsigned int Device, unsigned int Function, size_t Register, uint8_t Value);

/* PciDeviceRead
 * Reads a value of the given length from the given register
 * and this function takes care of the rest */
__EXTERN uint32_t PciDeviceRead(PciDevice_t *Device, 
    size_t Register, size_t Length);

/* PciDeviceWrite
 * Writes a value of the given length to the given register
 * and this function takes care of the rest */
__EXTERN void PciDeviceWrite(PciDevice_t *Device, 
    size_t Register, uint32_t Value, size_t Length);

/* PciReadVendorId
 * Reads the vendor id at given bus/device/function location */
__EXTERN uint16_t PciReadVendorId(PciBus_t *BusIo, 
    unsigned int Bus, unsigned int Device, unsigned int Function);

/* PciReadFunction
 * Reads in the pci header that exists at the given location
 * and fills out the information into <Pcs> */
__EXTERN void PciReadFunction(PciNativeHeader_t *Pcs,
    PciBus_t *BusIo, unsigned int Bus, unsigned int Device, unsigned int Function);

/* PciReadSecondaryBusNumber
 * Reads the secondary bus number at given pci device location
 * we can use this to get the bus-number behind a bridge */
__EXTERN uint8_t PciReadSecondaryBusNumber(PciBus_t *BusIo, 
    unsigned int Bus, unsigned int Device, unsigned int Function);

/* Reads the sub class at given location
 * Bit 7 - MultiFunction, Lower 4 bits is type.
 * Type 0 is standard, Type 1 is PCI-PCI Bridge,
 * Type 2 is CardBus Bridge */
__EXTERN uint8_t PciReadHeaderType(PciBus_t *BusIo,
    unsigned int Bus, unsigned int Device, unsigned int Function);

/* PciToString
 * Converts the given class, subclass and interface into
 * descriptive string to give the pci-entry a description */
__EXTERN
const char*
PciToString(
    _In_ uint8_t Class,
    _In_ uint8_t SubClass,
    _In_ uint8_t Interface);

#endif //!__X86_BUS_INTERFACE__

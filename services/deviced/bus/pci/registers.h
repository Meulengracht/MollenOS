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
 */

#ifndef __DEVICED_BUS_PCI_REGISTERS_H__
#define __DEVICED_BUS_PCI_REGISTERS_H__

#include <os/osdefs.h>

//  Standard capabilities live in the first 256 bytes of configuration space, 
// while PCI Express extended capabilities expand 
// from offset 0x100 up to 0xFFF (4096 bytes)
#define PCI_CONFIG_SPACE_SIZE                 256U

// Pci standard header offsets
#define PCI_VENDOR_ID_OFFSET                  0x00U
#define PCI_DEVICE_ID_OFFSET                  0x02U
#define PCI_COMMAND_OFFSET                    0x04U
#define PCI_STATUS_OFFSET                     0x06U
#define PCI_REVISION_OFFSET                   0x08U
#define PCI_INTERFACE_OFFSET                  0x09U
#define PCI_SUBCLASS_OFFSET                   0x0AU
#define PCI_CLASS_OFFSET                      0x0BU
#define PCI_CACHE_LINE_SIZE_OFFSET            0x0CU
#define PCI_LATENCY_TIMER_OFFSET              0x0DU
#define PCI_HEADER_TYPE_OFFSET                0x0EU

// Type 0 Header (Normal Device)
// 0x10 - 0x24 | Base Address Registers (BAR0 - BAR5)
// 0x28 | CardBus CIS Pointer
// 0x2C | Subsystem Vendor ID
// 0x2E | Subsystem ID
// 0x30 - 0x33 | Expansion ROM Base Address
#define PCI_CAPABILITY_POINTER_OFFSET         0x34U
#define PCI_INTERRUPT_LINE_OFFSET             0x3CU
#define PCI_INTERRUPT_PIN_OFFSET              0x3DU
// 0x3E | Min_Gnt: Burst period length the device expects.
// 0x3F | Max_Lat: Maximum latency the device can tolerate.

// Type 1 Header (PCI-to-PCI Bridge)
// 0x10 - 0x14 | Base Address Registers (BAR0 - BAR1): Memory/I/O base address registers for the bridge itself.
// 0x18 | Primary Bus Number: Bus number of the upstream bus.
// 0x19 | Secondary Bus Number: Bus number of the bus directly attached downstream.
// 0x1A | Subordinate Bus Number: Highest bus number behind the bridge.
// 0x1B | Secondary Latency Timer: Latency timer for the secondary bus.
// 0x1C | I/O Base: Bottom of the I/O address range for the secondary bus.
// 0x1D | I/O Limit: Top of the I/O address range for the secondary bus.
// 0x1E - 0x1F | Secondary Status: Status register for the secondary bus.
// 0x20 - 0x21 | Memory Base: Start of memory-mapped I/O range.
// 0x22 - 0x23 | Memory Limit: End of memory-mapped I/O range.
// 0x24 - 0x27 | Prefetchable Memory Base/Limit: Range for prefetchable memory.
// 0x28 - 0x2F | Prefetchable Base/Limit Upper 32 Bits: High bits for 64-bit prefetchable memory.
// 0x30 - 0x31 | I/O Base Upper 16 Bits: Upper 16 bits for extended I/O addressing.
// 0x32 - 0x33 | I/O Limit Upper 16 Bits: Upper 16 bits for extended I/O limit.
// 0x34 | Capabilities Pointer: Points to capability linked lists.
// 0x38 - 0x3B | Expansion ROM Base Address: Expansion ROM for the bridge.
// 0x3C | Interrupt Line / 0x3D | Interrupt Pin: Interrupt routing information.
// 0x3E - 0x3F | Bridge Control: Control flags for parity, SERR, secondary reset, and VGA/ISA routing.

#define PCI_STATUS_CAPABILITIES_LIST          0x10U

// Bit 7 indicates multi-function; bits 6-0 define Type 0x00 or 0x01.
#define PCI_HEADER_TYPE_NORMAL                0x00U
#define PCI_HEADER_TYPE_MASK                  0x7FU
#define PCI_HEADER_TYPE_MULTIFUNCTION         0x80U

#define PCI_CAPABILITY_LIST_MIN_OFFSET        0x40U
#define PCI_CAPABILITY_LIST_ALIGNMENT_MASK    0x03U

// PCI Capability Header Offsets
#define PCI_CAPABILITY_HEADER_LENGTH          2U

// The standard pci capability header offsets
#define PCI_CAPABILITY_ID_OFFSET              0U
#define PCI_CAPABILITY_NEXT_OFFSET            1U

// PCI Capability IDs
#define PCI_CAPABILITY_ID_MSI                 0x05U
#define PCI_CAPABILITY_ID_MSIX                0x11U

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
    uint16_t VendorId;             /* 0x00 */
    uint16_t DeviceId;             /* 0x02 */
    uint16_t Command;              /* 0x04 */
    uint16_t Status;               /* 0x06 */
    uint8_t  Revision;             /* 0x08 */
    uint8_t  Interface;            /* 0x09 */
    uint8_t  Subclass;             /* 0x0A */
    uint8_t  Class;                /* 0x0B */
    uint8_t  CacheLineSize;        /* 0x0C */
    uint8_t  LatencyTimer;         /* 0x0D */
    uint8_t  HeaderType;           /* 0x0E */
    uint8_t  Bist;                 /* 0x0F */
    uint32_t Bar0;                 /* 0x10 */
    uint32_t Bar1;                 /* 0x14 */
    uint32_t Bar2;                 /* 0x18 */
    uint32_t Bar3;                 /* 0x1C */
    uint32_t Bar4;                 /* 0x20 */
    uint32_t Bar5;                 /* 0x24 */
    uint32_t CardbusCISPtr;        /* 0x28 */
    uint16_t SubSystemVendorId;    /* 0x2C */
    uint16_t SubSystemId;          /* 0x2E */
    uint32_t ExpansionRomBaseAddr; /* 0x30 */
    uint32_t Reserved0;            /* 0x34 */
    uint32_t Reserved1;            /* 0x38 */
    uint8_t  InterruptLine;        /* 0x3C */
    uint8_t  InterruptPin;         /* 0x3D */
    uint8_t  MinGrant;             /* 0x3E */
    uint8_t  MaxLatency;           /* 0x3F */
});

#endif // __DEVICED_BUS_PCI_REGISTERS_H__

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

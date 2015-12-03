/* MollenOS
*
* Copyright 2011 - 2014, Philip Meulengracht
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
* MollenOS X86 PCI
* Version 1. PCI Support Only (No PCI Express)
*/

#ifndef _X86_PCI_H_
#define _X86_PCI_H_

/* Includes */
#include <crtdefs.h>
#include <stdint.h>

#ifndef __IRQHANDLER
#define __IRQHANDLER
typedef int(*IrqHandler_t)(void*);
#endif

/* Definitions */
#define X86_PCI_SELECT		0xCF8
#define X86_PCI_DATA		0xCFC

/* Structures */
typedef struct _PciNativeHeader
{
	/* 0x00 */
	uint16_t VendorId;
	uint16_t DeviceId;

	/* 0x04 */
	uint16_t Command;
	uint16_t Status;

	/* 0x08 */
	uint8_t  Revision;
	uint8_t  Interface;
	uint8_t  Subclass;
	uint8_t  Class;

	/* 0x0C */
	uint8_t  CacheLineSize;
	uint8_t  LatencyTimer;
	uint8_t  HeaderType;
	uint8_t  Bist;

	/* 0x10 */
	uint32_t Bar0;
	/* 0x14 */
	uint32_t Bar1;
	/* 0x18 */
	uint32_t Bar2;
	/* 0x1C */
	uint32_t Bar3;
	/* 0x20 */
	uint32_t Bar4;
	/* 0x24 */
	uint32_t Bar5;

	/* 0x28 */
	uint32_t CardbusCISPtr;
	/* 0x2C */
	uint16_t SubSystemVendorId;
	uint16_t SubSystemId;

	/* 0x30 */
	uint32_t ExpansionRomBaseAddr;

	/* 0x34 */
	uint32_t Reserved0;

	/* 0x38 */
	uint32_t Reserved1;

	/* 0x3C */
	uint8_t  InterruptLine;
	uint8_t  InterruptPin;
	uint8_t  MinGrant;
	uint8_t  MaxLatency;

} PciNativeHeader_t;

/* The Bus Header */
typedef struct _PciBus
{
	/* Address */
	Addr_t IoAddr;

	/* Type */
	uint32_t IsExtended;

	/* Pci Segment */
	uint32_t Segment;

	/* Bus Range */
	uint32_t BusStart;
	uint32_t BusEnd;

} PciBus_t;

/* The Driver Header */
typedef struct _PciDevice
{
	/* Type */
	uint32_t Type;

	/* Location */
	uint32_t Bus;
	uint32_t Device;
	uint32_t Function;

	/* Information (Header) */
	PciNativeHeader_t *Header;

	/* Children (list.h) */
	void *Parent;
	void *Children;

} PciDevice_t;

/* Types */
#define X86_PCI_TYPE_BRIDGE		0x1
#define X86_PCI_TYPE_DEVICE		0x2

#define X86_PCI_TYPE_IDE		0x2
#define X86_PCI_TYPE_AHCI		0x3
#define X86_PCI_TYPE_UHCI		0x4
#define X86_PCI_TYPE_OHCI		0x5
#define X86_PCI_TYPE_XHCI		0x6

#define PCI_DEVICE_CLASS_VIDEO		0x03
#define PCI_DEVICE_CLASS_BRIDGE		0x06

#define PCI_DEVICE_SUBCLASS_PCI		0x04

/* More defines */
#define X86_PCI_COMMAND_PORTIO		0x1
#define X86_PCI_COMMAND_MMIO		0x2
#define X86_PCI_COMMAND_BUSMASTER	0x4
#define X86_PCI_COMMAND_SPECIALCYC	0x8
#define X86_PCI_COMMAND_MEMWRITE	0x10
#define X86_PCI_COMMAND_VGAPALET	0x20
#define X86_PCI_COMMAND_PARRITYERR	0x40
#define X86_PCI_COMMAND_SERRENABLE	0x100
#define X86_PCI_COMMAND_FASTBTB		0x200
#define X86_PCI_COMMAND_INTDISABLE	0x400

/* Prototypes */

/* Initializor */
_CRT_EXTERN void PciEnumerate(void);

/* Read I/O */
_CRT_EXTERN uint8_t PciRead8(uint32_t Bus, uint32_t Device, uint32_t Function, uint32_t Register);
_CRT_EXTERN uint16_t PciRead16(uint32_t Bus, uint32_t Device, uint32_t Function, uint32_t Register);
_CRT_EXTERN uint32_t PciRead32(uint32_t Bus, uint32_t Device, uint32_t Function, uint32_t Register);
_CRT_EXPORT uint32_t PciDeviceRead(PciDevice_t *Device, uint32_t Register, uint32_t Length);

/* Write I/O */
_CRT_EXTERN void PciWrite8(uint32_t Bus, uint32_t Device, uint32_t Function, uint32_t Register, uint8_t Value);
_CRT_EXTERN void PciWrite16(uint32_t Bus, uint32_t Device, uint32_t Function, uint32_t Register, uint16_t Value);
_CRT_EXTERN void PciWrite32(uint32_t Bus, uint32_t Device, uint32_t Function, uint32_t Register, uint32_t Value);
_CRT_EXPORT void PciDeviceWrite(PciDevice_t *Device, uint32_t Register, uint32_t Value, uint32_t Length);

/* Install Pci Interrupt */
_CRT_EXPORT void InterruptInstallPci(PciDevice_t *PciDevice, IrqHandler_t Callback, void *Args);

/* Decode PCI Device to String */
_CRT_EXTERN const char *PciToString(uint8_t Class, uint8_t SubClass, uint8_t Interface);

/* Helpers */

/* Reads the vendor id at given location */
_CRT_EXTERN uint16_t PciReadVendorId(uint32_t Bus, uint32_t Device, uint32_t Function);
_CRT_EXTERN void PciReadFunction(PciNativeHeader_t *Pcs, uint32_t Bus, uint32_t Device, uint32_t Function);

_CRT_EXTERN uint8_t PciReadBaseClass(uint32_t Bus, uint32_t Device, uint32_t Function);
_CRT_EXTERN uint8_t PciReadSubclass(uint32_t Bus, uint32_t Device, uint32_t Function);
_CRT_EXTERN uint8_t PciReadSecondaryBusNumber(uint32_t Bus, uint32_t Device, uint32_t Function);
_CRT_EXTERN uint8_t PciReadHeaderType(uint32_t Bus, uint32_t Device, uint32_t Function);

#endif // !_X86_PCI_H_

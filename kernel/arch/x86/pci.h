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
* MollenOS X86-32 PCI
* Version 1. PCI Support Only (No PCI Express)
*/

#ifndef _X86_PCI_H_
#define _X86_PCI_H_

/* Includes */
#include <crtdefs.h>
#include <stdint.h>

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
	struct _PciNativeHeader *Header;

	/* Children (list.h) */
	void *Parent;
	void *Children;

} PciDevice_t;

/* Internal Use */
typedef struct _PciIrqResource
{
	/* Double Voids */
	void *Device;
	void *Table;

} PciIrqResource_t;

/* This doesn't fully support linked entries */
#pragma pack(push, 1)
typedef struct _PciRoutings
{
	/* Just a lot of ints */
	int Interrupts[128];
	uint8_t Trigger[128];
	uint8_t Shareable[128];
	uint8_t Polarity[128];
	uint8_t Fixed[128];

} PciRoutings_t;
#pragma pack(pop)

#pragma pack(push, 1)
typedef struct _PciAcpiDevice
{
	/* Type */
	uint32_t Type;

	/* ACPI_HANDLE */
	void *Handle;

	/* Irq Routings */
	struct _PciRoutings *Routings;

	/* Bus Id */
	char BusId[8];

	/* PCI Location */
	uint32_t Bus;
	uint32_t Device;
	uint32_t Function;
	uint32_t Segment;

	/* Supported NS Functions */
	uint32_t Features;

	/* Current Status */
	uint32_t Status;

	/* Bus Address */
	uint64_t Address;

	/* Hardware Id */
	char HID[16];

	/* Unique Id */
	char UID[16];

	/* Compatible Id's */
	void *CID;

	/* Type Features */
	uint64_t xFeatures;

} PciAcpiDevice_t;
#pragma pack(pop)


/* Feature Flags */
#define X86_ACPI_FEATURE_STA	0x1
#define X86_ACPI_FEATURE_CID	0x2
#define X86_ACPI_FEATURE_RMV	0x4
#define X86_ACPI_FEATURE_EJD	0x8
#define X86_ACPI_FEATURE_LCK	0x10
#define X86_ACPI_FEATURE_PS0	0x20
#define X86_ACPI_FEATURE_PRW	0x40
#define X86_ACPI_FEATURE_ADR	0x80
#define X86_ACPI_FEATURE_HID	0x100
#define X86_ACPI_FEATURE_UID	0x200
#define X86_ACPI_FEATURE_PRT	0x400
#define X86_ACPI_FEATURE_BBN	0x800
#define X86_ACPI_FEATURE_SEG	0x1000
#define X86_ACPI_FEATURE_REG	0x2000
#define X86_ACPI_FEATURE_CRS	0x4000

/* Type Definitions */
#define ACPI_BUS_SYSTEM			0x0
#define ACPI_BUS_TYPE_DEVICE	0x1
#define ACPI_BUS_TYPE_PROCESSOR	0x2
#define ACPI_BUS_TYPE_THERMAL	0x3
#define ACPI_BUS_TYPE_POWER		0x4
#define ACPI_BUS_TYPE_SLEEP		0x5
#define ACPI_BUS_TYPE_PWM		0x6
#define ACPI_BUS_ROOT_BRIDGE	0x7


/* Video Features */
#define ACPI_VIDEO_SWITCHING	0x1
#define ACPI_VIDEO_ROM			0x2
#define ACPI_VIDEO_POSTING		0x4
#define ACPI_VIDEO_BACKLIGHT	0x8
#define ACPI_VIDEO_BRIGHTNESS	0x10

/* Types */
#define X86_PCI_TYPE_BRIDGE		0x1
#define X86_PCI_TYPE_DEVICE		0x2


#define X86_PCI_TYPE_IDE		0x2
#define X86_PCI_TYPE_AHCI		0x3
#define X86_PCI_TYPE_UHCI		0x4
#define X86_PCI_TYPE_OHCI		0x5
#define X86_PCI_TYPE_XHCI		0x6

/* This is information */
typedef struct _PciDeviceInformation
{
	/* Pci Vendor Id */
	uint32_t DeviceId;

	/* String */
	char *String;

} PciDeviceInformation_t;

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

/* Read I/O */
_CRT_EXTERN uint8_t PciReadByte(const uint16_t Bus, const uint16_t Device,
							   const uint16_t Function, const uint32_t Register);
_CRT_EXTERN uint16_t PciReadWord(const uint16_t Bus, const uint16_t Device,
								 const uint16_t Function, const uint32_t Register);
_CRT_EXTERN uint32_t PciReadDword(const uint16_t Bus, const uint16_t Device,
								  const uint16_t Function, const uint32_t Register);

/* Write I/O */
_CRT_EXTERN void PciWriteByte(const uint16_t Bus, const uint16_t Device,
							  const uint16_t Function, const uint32_t Register, uint8_t Value);
_CRT_EXTERN void PciWriteWord(const uint16_t Bus, const uint16_t Device,
							  const uint16_t Function, const uint32_t Register, uint16_t Value);
_CRT_EXTERN void PciWriteDword(const uint16_t Bus, const uint16_t Device,
							   const uint16_t Function, const uint32_t Register, uint32_t Value);

/* Install PCI Interrupt */
_CRT_EXTERN void InterruptInstallPci(PciDevice_t *PciDevice, IrqHandler_t Callback, void *Args);

/* Get Irq by Bus / Dev / Pin
* Returns -1 if no overrides exists */
_CRT_EXTERN int PciDeviceGetIrq(uint32_t Bus, uint32_t Device, uint32_t Pin,
								uint8_t *TriggerMode, uint8_t *Polarity, uint8_t *Shareable,
								uint8_t *Fixed);

/* Decode PCI Device to String */
_CRT_EXTERN char *PciToString(uint8_t Class, uint8_t SubClass, uint8_t Interface);

/* Reads the vendor id at given location */
_CRT_EXTERN uint16_t PciReadVendorId(const uint16_t Bus, const uint16_t Device, const uint16_t Function);

/* Reads a PCI header at given location */
_CRT_EXTERN void PciReadFunction(PciNativeHeader_t *Pcs, const uint16_t Bus, const uint16_t Device, const uint16_t Function);

/* Reads the base class at given location */
_CRT_EXTERN uint8_t PciReadBaseClass(const uint16_t Bus, const uint16_t Device, const uint16_t Function);

/* Reads the sub class at given location */
_CRT_EXTERN uint8_t PciReadSubclass(const uint16_t Bus, const uint16_t Device, const uint16_t Function);

/* Reads the secondary bus number at given location */
_CRT_EXTERN uint8_t PciReadSecondaryBusNumber(const uint16_t Bus, const uint16_t Device, const uint16_t Function);

/* Reads the sub class at given location 
 * Bit 7 - MultiFunction, Lower 4 bits is type.
 * Type 0 is standard, Type 1 is PCI-PCI Bridge,
 * Type 2 is CardBus Bridge */
_CRT_EXTERN uint8_t PciReadHeaderType(const uint16_t Bus, const uint16_t Device, const uint16_t Function);

#endif // !_X86_PCI_H_

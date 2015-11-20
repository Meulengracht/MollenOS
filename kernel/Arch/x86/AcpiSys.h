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
* MollenOS x86 ACPI Interface (Uses ACPICA)
*/

#ifndef _X86_ACPI_SYSTEM_
#define _X86_ACPI_SYSTEM_

/* Includes */
#include <stdint.h>
#include <crtdefs.h>
#include <acpi.h>

/* Definitions */


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

/* Battery Features */
#define ACPI_BATTERY_NORMAL		0x1
#define ACPI_BATTERY_EXTENDED	0x2
#define ACPI_BATTERY_QUERY		0x4
#define ACPI_BATTERY_CHARGEINFO	0x8
#define ACPI_BATTERY_CAPMEAS	0x10

/* Structures */

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
typedef struct _AcpiDevice
{
	/* Type */
	uint32_t Type;

	/* ACPI_HANDLE */
	void *Handle;

	/* Irq Routings */
	PciRoutings_t *Routings;

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

} AcpiDevice_t;
#pragma pack(pop)


/* Prototypes */

/* Initializes Early access and enumerates ACPI Tables */
_CRT_EXTERN void AcpiEnumerate(void);

/* Initializes FULL access across ACPICA */
_CRT_EXTERN void AcpiSetupFull(void);

/* Scan all Acpi Devices */
_CRT_EXTERN void AcpiScan(void);

/* Device Functions */
_CRT_EXTERN ACPI_STATUS AcpiDeviceAttachData(AcpiDevice_t *Device, uint32_t Type);
_CRT_EXTERN int32_t AcpiDeviceGetIrq(uint32_t Bus, uint32_t Device, uint32_t Pin, 
	uint8_t *TriggerMode, uint8_t *Polarity, uint8_t *Shareable, uint8_t *Fixed);

/* Device Get's */
_CRT_EXTERN ACPI_STATUS AcpiDeviceGetStatus(AcpiDevice_t* Device);
_CRT_EXTERN ACPI_STATUS AcpiDeviceGetBusAndSegment(AcpiDevice_t* Device);
_CRT_EXTERN ACPI_STATUS AcpiDeviceGetBusId(AcpiDevice_t *Device, uint32_t Type);
_CRT_EXTERN ACPI_STATUS AcpiDeviceGetFeatures(AcpiDevice_t *Device);
_CRT_EXTERN ACPI_STATUS AcpiDeviceGetIrqRoutings(AcpiDevice_t *Device);
_CRT_EXTERN ACPI_STATUS AcpiDeviceGetHWInfo(AcpiDevice_t *Device, ACPI_HANDLE ParentHandle, uint32_t Type);

/* Device Type Helpers */
_CRT_EXTERN ACPI_STATUS AcpiDeviceIsVideo(AcpiDevice_t *Device);
_CRT_EXTERN ACPI_STATUS AcpiDeviceIsDock(AcpiDevice_t *Device);
_CRT_EXTERN ACPI_STATUS AcpiDeviceIsBay(AcpiDevice_t *Device);
_CRT_EXTERN ACPI_STATUS AcpiDeviceIsBattery(AcpiDevice_t *Device);

#endif //!_X86_ACPI_SYSTEM_
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
* MollenOS ACPI Interface (Uses ACPICA)
*/

#ifndef _MOLLENOS_ACPI_SYSTEM_
#define _MOLLENOS_ACPI_SYSTEM_

/* Includes */
#include <stdint.h>
#include <crtdefs.h>
#include <ds/list.h>

/* ACPICA Header */
#include <acpi.h>

/* Definitions */
#define ACPI_MAX_INIT_TABLES	16

#define ACPI_NOT_AVAILABLE		0
#define ACPI_AVAILABLE			1

/* Feature Flags */
#define ACPI_FEATURE_STA		0x1
#define ACPI_FEATURE_CID		0x2
#define ACPI_FEATURE_RMV		0x4
#define ACPI_FEATURE_EJD		0x8
#define ACPI_FEATURE_LCK		0x10
#define ACPI_FEATURE_PS0		0x20
#define ACPI_FEATURE_PRW		0x40
#define ACPI_FEATURE_ADR		0x80
#define ACPI_FEATURE_HID		0x100
#define ACPI_FEATURE_UID		0x200
#define ACPI_FEATURE_PRT		0x400
#define ACPI_FEATURE_BBN		0x800
#define ACPI_FEATURE_SEG		0x1000
#define ACPI_FEATURE_REG		0x2000
#define ACPI_FEATURE_CRS		0x4000

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

/* First we declare an interrupt entry
 * a very small structure containing information
 * about an interrupt in this system */
#pragma pack(push, 1)
typedef struct _PciRoutingEntry
{
	/* The interrupt line */
	int Interrupts;

	/* Interrupt information */
	uint8_t Trigger;
	uint8_t Shareable;
	uint8_t Polarity;
	uint8_t Fixed;

} PciRoutingEntry_t;

/* A table containing 128 interrupt entries 
 * which is the number of 'redirects' there can
 * be */
typedef struct _PciRoutings
{
	/* This descripes whether or not 
	 * an entry is a list or .. not a list */
	int InterruptInformation[128];

	/* Just a table of 128 interrupts or
	 * 128 lists of interrupts */
	union {
		PciRoutingEntry_t *Entry;
		List_t *Entries;
	} Interrupts[128];

} PciRoutings_t;
#pragma pack(pop)

#pragma pack(push, 1)
typedef struct _AcpiDevice
{
	/* Name */
	char *Name;

	/* Type */
	int Type;

	/* ACPI_HANDLE */
	void *Handle;

	/* Irq Routings */
	PciRoutings_t *Routings;

	/* Bus Id */
	char BusId[8];

	/* PCI Location */
	int Bus;
	int Device;
	int Function;
	int Segment;

	/* Supported NS Functions */
	size_t Features;

	/* Current Status */
	size_t Status;

	/* Bus Address */
	uint64_t Address;

	/* Hardware Id */
	char HID[16];

	/* Unique Id */
	char UID[16];

	/* Compatible Id's */
	void *CID;

	/* Type Features */
	int xFeatures;

} AcpiDevice_t;
#pragma pack(pop)


/* Initialize functions, don't call these manually
 * they get automatically called in kernel setup */

/* Initializes Early access and enumerates 
 * ACPI Tables, returns -1 if ACPI is not
 * present on this system */
__EXTERN int AcpiEnumerate(void);

/* Initializes the full access and functionality
 * of ACPICA / ACPI and allows for scanning of 
 * ACPI devices */
__EXTERN void AcpiInitialize(void);

/* Scans the ACPI bus/namespace for all available
 * ACPI devices/functions and initializes them */
__EXTERN void AcpiScan(void);

/* This returns ACPI_NOT_AVAILABLE if ACPI is not available
 * on the system, or ACPI_AVAILABLE if acpi is available */
__EXTERN int AcpiAvailable(void);

/* Lookup a bridge device for the given
 * bus that contains pci routings */
__EXTERN AcpiDevice_t *AcpiLookupDevice(int Bus);

/* Device Functions */
__EXTERN ACPI_STATUS AcpiDeviceAttachData(AcpiDevice_t *Device, uint32_t Type);

/* Device Get's */
__EXTERN ACPI_STATUS AcpiDeviceGetStatus(AcpiDevice_t* Device);
__EXTERN ACPI_STATUS AcpiDeviceGetBusAndSegment(AcpiDevice_t* Device);
__EXTERN ACPI_STATUS AcpiDeviceGetBusId(AcpiDevice_t *Device, uint32_t Type);
__EXTERN ACPI_STATUS AcpiDeviceGetFeatures(AcpiDevice_t *Device);
__EXTERN ACPI_STATUS AcpiDeviceGetIrqRoutings(AcpiDevice_t *Device);
__EXTERN ACPI_STATUS AcpiDeviceGetHWInfo(AcpiDevice_t *Device, ACPI_HANDLE ParentHandle, uint32_t Type);

/* Device Type Helpers */
__EXTERN ACPI_STATUS AcpiDeviceIsVideo(AcpiDevice_t *Device);
__EXTERN ACPI_STATUS AcpiDeviceIsDock(AcpiDevice_t *Device);
__EXTERN ACPI_STATUS AcpiDeviceIsBay(AcpiDevice_t *Device);
__EXTERN ACPI_STATUS AcpiDeviceIsBattery(AcpiDevice_t *Device);

#endif //!_MOLLENOS_ACPI_SYSTEM_
/* MollenOS
*
* Copyright 2011 - 2016, Philip Meulengracht
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
* MollenOS - Device Manager
*/

#ifndef _MCORE_DRIVER_MANAGER_H_
#define _MCORE_DRIVER_MANAGER_H_

/* Includes */
#include <os/osdefs.h>
#include "../Arch/Arch.h"
#include <Devices/Video.h>
#include <Events.h>

/* Fixed Classes */
#define DEVICEMANAGER_LEGACY_CLASS		0x0000015A
#define DEVICEMANAGER_ACPI_CLASS		0x0000AC71

/* Fixed ACPI Sub Classes */
#define DEVICEMANAGER_ACPI_HPET			0x00000008

/* Erhhh, limitiations? */
#define DEVICEMANAGER_MAX_IO_SIZE		(512 * 1024)
#define DEVICEMANAGER_MAX_IRQS			8
#define DEVICEMANAGER_MAX_IOSPACES		6

/* Device Types */
typedef enum _DeviceType
{
	DeviceUnknown = 0,
	DeviceCpu,
	DeviceCpuCore,
	DeviceController,
	DeviceBus,
	DeviceClock,
	DeviceTimer,
	DevicePerfTimer,
	DeviceInput,
	DeviceStorage,
	DeviceVideo

} DeviceType_t;

typedef enum _DeviceResourceType
{
	ResourceIrq

} DeviceResourceType_t;

typedef enum _DeviceRequestType
{
	RequestQuery,
	RequestRead,
	RequestWrite,
	RequestInstall

} DeviceRequestType_t;

typedef enum _DeviceErrorMessage
{
	RequestNoError,
	RequestInvalidParameters,
	RequestDeviceError,
	RequestDeviceIsRemoved

} DeviceErrorMessage_t;

typedef enum _DriverStatus
{
	DriverNone,
	DriverLoaded,
	DriverActive,
	DriverStopped

} DriverStatus_t;

/* Structures */
typedef struct _MCoreDriver
{
	/* Name */
	char *Name;

	/* Version */
	int Version;

	/* Status */
	DriverStatus_t Status;

	/* Data */
	void *Data;

} MCoreDriver_t;

typedef struct _MCoreDevice
{
	/* System Id */
	UUId_t Id;

	/* Name */
	char *Name;

	/* Device 
	 * Information 
	 * Used to match with
	 * a driver */
	DevInfo_t VendorId;
	DevInfo_t DeviceId;
	DevInfo_t Class;
	DevInfo_t Subclass;

	/* Type */
	DeviceType_t Type;

	/* Irq Information */
	int IrqLine;
	int IrqPin;
	int IrqAvailable[DEVICEMANAGER_MAX_IRQS];

	/* I/O Spaces */
	DeviceIoSpace_t *IoSpaces[DEVICEMANAGER_MAX_IOSPACES];

	/* Driver */
	MCoreDriver_t Driver;

	/* Bus Device & Information */
	DevInfo_t Segment;
	DevInfo_t Bus;
	DevInfo_t Device;
	DevInfo_t Function;
	void *BusDevice;

	/* Additional Data */
	void *Data;

} MCoreDevice_t;

/* Boot Video */
__EXTERN void DmRegisterBootVideo(MCoreDevice_t *Video);

#endif //_MCORE_DRIVER_MANAGER_H_
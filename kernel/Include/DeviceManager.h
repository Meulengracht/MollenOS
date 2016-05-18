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
#include "../Arch/Arch.h"
#include <Devices/Video.h>
#include <stdint.h>
#include <Mutex.h>

/* Definitions */
typedef unsigned DevInfo_t;
typedef int DevId_t;

/* Fixed Classes */
#define DEVICEMANAGER_LEGACY_CLASS		0x0000015A
#define DEVICEMANAGER_ACPI_CLASS		0x0000AC71

/* Fixed ACPI Sub Classes */
#define DEVICEMANAGER_ACPI_HPET			0x00000008

/* Erhhh, limitiations? */
#define DEVICEMANAGER_MAX_IO_SIZE		(16 * 1024)
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

typedef enum _DeviceRequestStatus
{
	RequestPending,
	RequestInProgress,
	RequestOk,
	RequestInvalidParameters,
	RequestDeviceError,
	RequestDeviceIsRemoved

} DeviceRequestStatus_t;

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
	DevId_t Id;

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
	IrqHandler_t IrqHandler;

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

/* Device Requests */
#pragma pack(push, 1)
typedef struct _MCoreDeviceRequest
{
	/* Request Type */
	DeviceRequestType_t Type;

	/* Device Id */
	DevId_t DeviceId;

	/* Data */
	uint64_t SectorLBA;
	uint8_t *Buffer;
	size_t Length;

	/* Result */
	DeviceRequestStatus_t Status;

} MCoreDeviceRequest_t;
#pragma pack(pop)

/* Prototypes */
_CRT_EXTERN void DmInit(void);
_CRT_EXTERN void DmStart(void);

/* Boot Video */
_CRT_EXTERN void DmRegisterBootVideo(MCoreDevice_t *Video);

/* Setup of devices */
_CRT_EXPORT int DmRequestResource(MCoreDevice_t *Device, DeviceResourceType_t ResourceType);
_CRT_EXPORT DevId_t DmCreateDevice(char *Name, MCoreDevice_t *Device);
_CRT_EXPORT MCoreDevice_t *DmGetDevice(DeviceType_t Type);
_CRT_EXPORT void DmDestroyDevice(DevId_t DeviceId);

/* Device Requests */
_CRT_EXPORT void DmCreateRequest(MCoreDeviceRequest_t *Request);
_CRT_EXPORT void DmWaitRequest(MCoreDeviceRequest_t *Request, size_t Timeout);

#endif //_MCORE_DRIVER_MANAGER_H_
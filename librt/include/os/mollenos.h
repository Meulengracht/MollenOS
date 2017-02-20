/* MollenOS
 *
 * Copyright 2011 - 2017, Philip Meulengracht
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
 * MollenOS MCore - Definitions & Structures
 * - This header describes the os-structure, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#ifndef _MOLLENOS_INTERFACE_H_
#define _MOLLENOS_INTERFACE_H_

/* Includes
 * System */
#include <os/osdefs.h>

/* This describes a window handle 
 * used by UI functions */
#ifndef MWNDHANDLE
#define MWNDHANDLE
typedef void *WndHandle_t;
#endif

/* Enumerators */
typedef enum _MollenOSDeviceType {
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
} OSDeviceType_t;

/* Structures */
typedef struct _MollenOSVideoDescriptor
{
	/* Framebuffer */
	void *FrameBufferAddr;

	/* Mode Information */
	size_t BytesPerScanline;
	size_t Height;
	size_t Width;
	int Depth;

	/* Pixel Information */
	int RedPosition;
	int BluePosition;
	int GreenPosition;
	int ReservedPosition;

	int RedMask;
	int BlueMask;
	int GreenMask;
	int ReservedMask;

} OSVideoDescriptor_t;

/* Memory Descriptor
 * Describes the current memory state and setup
 * thats available on the current machine */
PACKED_TYPESTRUCT(MemoryDescriptor, {
	size_t			PagesTotal;
	size_t			PagesUsed;
	size_t			PageSizeBytes;
});

/* This is only hardcoded for now untill
 * we implement support for querying memory
 * options */
#ifndef MOS_PAGE_SIZE
#define MOS_PAGE_SIZE		0x1000
#endif

/* The max-path we support in the OS
 * for file-paths, in MollenOS we support
 * rather long paths */
#define _MAXPATH			512

/* Cpp Guard */
_CODE_BEGIN

/* MemoryShare
 * This shares a piece of memory with the 
 * target process. The function returns NULL
 * on failure to share the piece of memory
 * otherwise it returns the new buffer handle
 * that can be accessed by the other process */
_MOS_API 
void *
MemoryShare(
	_In_ UUId_t Process, 
	_In_ void *Buffer, 
	_In_ size_t Size);

/* MemoryUnshare
 * This takes a previous shared memory handle 
 * and unshares it again from the target process */
_MOS_API 
OsStatus_t 
MemoryUnshare(
	_In_ UUId_t Process,
	_In_ void *MemoryHandle, 
	_In_ size_t Size);

/* MemoryQuery
 * Queries the underlying system for memory information 
 * like memory used and the page-size */
_MOS_API
OsStatus_t
MemoryQuery(
	_Out_ MemoryDescriptor_t *Descriptor);

/* DeviceQuery
 * Queries the given device-type for information */
_MOS_API 
OsStatus_t 
DeviceQuery(
	_In_ OSDeviceType_t Type, 
	_In_ int Request, 
	_Out_ void *Buffer, 
	_In_ size_t Length);

/* ScreenQueryGeometry
 * This function returns screen geomemtry
 * descriped as a rectangle structure */
_MOS_API 
OsStatus_t 
ScreenQueryGeometry(
	_Out_ Rect_t *Rectangle);

/* PathQueryWorkingDirectory
 * Queries the current working directory path
 * for the current process (See _MAXPATH) */
_MOS_API
OsStatus_t
PathQueryWorkingDirectory(
	_Out_ char *Buffer,
	_In_ size_t MaxLength);

/* PathChangeWorkingDirectory
 * Performs changes to the current working directory
 * by canonicalizing the given path modifier or absolute
 * path */
_MOS_API
OsStatus_t
PathChangeWorkingDirectory(
	_In_ __CONST char *Path);

/* PathQueryApplication
 * Queries the application path for
 * the current process (See _MAXPATH) */
_MOS_API
OsStatus_t
PathQueryApplication(
	_Out_ char *Buffer,
	_In_ size_t MaxLength);

/***********************
 * System Misc Prototypes
 * - No functions here should
 *   be called manually
 *   they are automatically used
 *   by systems
 ***********************/
_MOS_API int WaitForSignal(size_t Timeout);
_MOS_API int SignalProcess(UUId_t Target);
_MOS_API void MollenOSSystemLog(const char *Format, ...);
_MOS_API int MollenOSEndBoot(void);
_MOS_API int MollenOSRegisterWM(void);

_CODE_END

#endif //!_MOLLENOS_INTERFACE_H_

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
* MollenOS C Library - Standard OS Header
*/

#ifndef __MOLLENOS_CLIB__
#define __MOLLENOS_CLIB__

/* C-Library - Includes */
#include <crtdefs.h>

/* OS - Includes */
#include <os/Ipc.h>

/* Definitons */

/* The max path to expect from mollenos 
 * file/path operations */
#ifndef MPATH_MAX
#define MPATH_MAX		512
#endif

/* Enumerators */
typedef enum _MollenOSDeviceType
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

} MollenOSDeviceType_t;

/* Environment Paths */
typedef enum _EnvironmentPaths
{
	/* The default */
	PathCurrentWorkingDir = 0,

	/* Application Paths */
	PathApplicationBase,
	PathApplicationData,

	/* System Directories */
	PathSystemBase,
	PathSystemDirectory,

	/* Shared Directories */
	PathCommonBin,
	PathCommonDocuments,
	PathCommonInclude,
	PathCommonLib,
	PathCommonMedia,

	/* User Directories */
	PathUserBase,

	/* Special Directory Count */
	PathEnvironmentCount

} EnvironmentPath_t;

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

} MollenOSVideoDescriptor_t;

typedef struct _mRectangle
{
	/* Origin */
	int x, y;

	/* Size */
	int h, w;

} Rect_t;

/***********************
* IPC Prototypes
***********************/

/* IPC - Peek - NO BLOCK
 * This returns -1 if there is no new messages
 * in the message-queue, otherwise it returns 0
 * and fills the base structures with information about
 * the message */
_MOS_API int MollenOSMessagePeek(MEventMessage_t *Message);

/* IPC - Read/Wait - BLOCKING OP
 * This returns -1 if something went wrong reading
 * a message from the message queue, otherwise it returns 0
 * and fills the structures with information about
 * the message */
_MOS_API int MollenOSMessageWait(MEventMessage_t *Message);

/* IPC - Write
 * Returns -1 if message failed to send
 * Returns -2 if message-target didn't exist
 * Returns 0 if message was sent correctly to target */
_MOS_API int MollenOSMessageSend(IpcComm_t Target, void *Message, size_t MessageLength);

/***********************
* Memory Prototypes
***********************/

/* Memory - Share 
 * This shares a piece of memory with the 
 * target process. The function returns NULL
 * on failure to share the piece of memory
 * otherwise it returns the new buffer handle
 * that can be accessed by the other process */
_MOS_API void *MollenOSMemoryShare(IpcComm_t Target, void *Buffer, size_t Size);

/* Memory - Unshare 
 * This takes a previous shared memory handle 
 * and unshares it again from the target process
 * The function returns 0 if unshare was succesful, 
 * otherwise -1 */
_MOS_API int MollenOSMemoryUnshare(IpcComm_t Target, void *MemoryHandle, size_t Size);

/***********************
* Device Prototypes
***********************/
_MOS_API int MollenOSDeviceQuery(MollenOSDeviceType_t Type, int Request, void *Buffer, size_t Length);

/***********************
* Shared Library Prototypes
***********************/
_MOS_API void *SharedObjectLoad(const char *SharedObject);
_MOS_API void *SharedObjectGetFunction(void *Handle, const char *Function);
_MOS_API void SharedObjectUnload(void *Handle);

/***********************
* Environment Prototypes
***********************/
_MOS_API void EnvironmentResolve(EnvironmentPath_t SpecialPath, char *StringBuffer);

/***********************
* Screen Prototypes
***********************/
_MOS_API void MollenOSGetScreenGeometry(Rect_t *Rectangle);

/***********************
* System Misc Prototypes
***********************/
_MOS_API void MollenOSSystemLog(const char *Message);
_MOS_API int MollenOSEndBoot(void);
_MOS_API int MollenOSRegisterWM(void);

#endif //!__MOLLENOS_CLIB__
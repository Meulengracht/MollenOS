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
* MollenOS - Graphical UI Functions
*/

/* Includes */
#include <os/MollenOS.h>
#include <os/Syscall.h>
#include <os/Thread.h>

/* C Library */
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

/* UI Ipc */
#include "../../../userspace/System/MWinMgr/Core/WindowIpc.h"

/* Kernel Guard */
#ifdef LIBC_KERNEL
void __UILibCEmpty(void)
{
}
#else

/* Private structures */
typedef struct _MWindowDescriptor
{
	/* Id of window */
	int WindowId;

	/* Window dimensions */
	Rect_t Dimensions;

	/* Backbuffer information */
	void *Backbuffer;
	size_t BackbufferSize;

} WindowDescriptor_t;

/* Globals */
void *__UiConnectionBuffer = NULL;
void *__UiConnectionHandle = NULL;
int __UiConnected = 0;
Spinlock_t __UiConnectionLock;

/* UiConnect
 * Only called once, it initializes 
 * the ui-connection to the window
 * server */
void UiConnect(void)
{
	/* Sanity */
	if (__UiConnected == 1)
		return;

	/* Allocate a message buffer 
	 * 512 bytes should do it */
	__UiConnectionBuffer = malloc(512);

	/* Window server is ALWAYS process id 0 */
	__UiConnectionHandle = 
		MollenOSMemoryShare(0, __UiConnectionBuffer, 512);

	/* Reset lock */
	SpinlockReset(&__UiConnectionLock);

	/* Done! */
	__UiConnected = 1;
}

/* UiDisconnect 
 * Destroys and cleans up the ui 
 * connection to the server */
void UiDisconnect(void)
{
	/* Sanity */
	if (__UiConnected != 1)
		return;

	/* Unshare the memory */
	MollenOSMemoryUnshare(0, __UiConnectionHandle, 512);

	/* Free the handle */
	free(__UiConnectionBuffer);

	/* Done! */
	__UiConnected = 0;
}

/* UiCreateWindow 
 * Creates a window of the given
 * dimensions and flags. The returned
 * value is the id of the newly created
 * window. Returns NULL on failure */
WndHandle_t UiCreateWindow(Rect_t Dimensions, int Flags)
{
	/* Variables */
	IPCWindowCreate_t *WndInformation;
	MEventMessageGeneric_t Message;
	WindowDescriptor_t *WndDescriptor;

	/* Sanity */
	if (__UiConnected != 1)
		return NULL;

	/* Grab buffer lock */
	SpinlockAcquire(&__UiConnectionLock);

	/* Access/Setup buffer */
	WndInformation = (IPCWindowCreate_t*)__UiConnectionBuffer;
	memset(WndInformation, 0, sizeof(IPCWindowCreate_t));

	/* Fill in request information */
	memcpy(&WndInformation->Dimensions, &Dimensions, sizeof(Rect_t));
	WndInformation->Flags = Flags;

	/* Setup base message */
	Message.Header.Length = sizeof(MEventMessageGeneric_t);
	Message.Header.Type = EventGeneric;
	
	/* Setup generic message */
	Message.Type = GenericWindowCreate;
	Message.LoParam = (size_t)__UiConnectionHandle;
	Message.HiParam = 0;

	/* Send request */
	if (MollenOSMessageSend(0, &Message, Message.Header.Length)) {
		SpinlockRelease(&__UiConnectionLock);
		return NULL;
	}

	/* Save return information
	 * especially the backbuffer information */
	WndDescriptor = (WindowDescriptor_t*)malloc(sizeof(WindowDescriptor_t));
	WndDescriptor->WindowId = WndInformation->WindowId;
	WndDescriptor->Backbuffer = WndInformation->Backbuffer;
	WndDescriptor->BackbufferSize = WndInformation->BackbufferSize;
	memcpy(&WndDescriptor->Dimensions, &WndInformation->ResultDimensions,
		sizeof(Rect_t));

	/* Release buffer lock */
	SpinlockRelease(&__UiConnectionLock);

	/* Done */
	return WndDescriptor;
}

/* UiDestroyWindow 
 * Destroys a given window 
 * and frees the resources associated
 * with it. Returns 0 on success */
int UiDestroyWindow(WndHandle_t Handle)
{
	/* Variables */
	MEventMessageGeneric_t Message;
	WindowDescriptor_t *Wnd = 
		(WindowDescriptor_t*)Handle;

	/* Sanity */
	if (__UiConnected != 1)
		return -1;

	/* Setup base message */
	Message.Header.Length = sizeof(MEventMessageGeneric_t);
	Message.Header.Type = EventGeneric;

	/* Setup generic message */
	Message.Type = GenericWindowDestroy;
	Message.LoParam = (size_t)Wnd->WindowId;
	Message.HiParam = 0;

	/* Send request */
	if (MollenOSMessageSend(0, &Message, Message.Header.Length)) {
		return -1;
	}

	/* Free resources */
	free(Wnd);

	/* Done */
	return 0;
}

#endif
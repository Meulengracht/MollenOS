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
#include <os/Ui.h>
#include <os/Syscall.h>
#include <os/Thread.h>
#include "Shared/BAlloc.h"

/* C Library */
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

/* UI Ipc */
#include "../../../userspace/System/MWinMgr/Core/WindowIpc.h"

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
Spinlock_t __UiConnectionLock;
int __UiConnected = 0;
UiType_t __UiType = UiNone;

/* Helpers */
#define TO_SHARED_HANDLE(Ptr) ((void*)((uint8_t*)__UiConnectionHandle + ((size_t)Ptr - (size_t)__UiConnectionBuffer)))

/* UiConnect
 * Only called once, it initializes 
 * the ui-connection to the window
 * server */
void UiConnect(UiType_t UiType)
{
	/* Sanity */
	if (__UiConnected == 1)
		return;

	/* Allocate a message buffer 
	 * 1024 bytes should do it */
	__UiConnectionBuffer = malloc(1024);

	/* Window server is ALWAYS process id 1 */
	__UiConnectionHandle = 
		MollenOSMemoryShare(1, __UiConnectionBuffer, 1024);

	/* Reset lock */
	SpinlockReset(&__UiConnectionLock);

	/* Create the message allocator */
	bpool(__UiConnectionBuffer, 1024); 

	/* Done! */
	__UiType = UiType;
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
	MollenOSMemoryUnshare(1, __UiConnectionHandle, 1024);

	/* Free the handle */
	free(__UiConnectionBuffer);

	/* Done! */
	__UiConnected = 0;
}

/* UiQueryProcessWindow 
 * Queries the window information
 * from a target process id */
int UiQueryProcessWindow(IpcComm_t Target, IPCWindowQuery_t *Information)
{
	/* Variables */
	IPCWindowQuery_t *QueryInfo;
	MEventMessageGeneric_t Message;
	void *SharedPtr = NULL;

	/* Grab buffer lock */
	SpinlockAcquire(&__UiConnectionLock);

	/* Access/Setup buffer */
	QueryInfo = (IPCWindowQuery_t*)bget(sizeof(IPCWindowQuery_t));
	memset(QueryInfo, 0, sizeof(IPCWindowQuery_t));

	QueryInfo->Target = Target;

	/* Release buffer lock */
	SpinlockRelease(&__UiConnectionLock);

	/* Cast to shared handle */
	SharedPtr = TO_SHARED_HANDLE(QueryInfo);

	/* Setup base message */
	Message.Header.Length = sizeof(MEventMessageGeneric_t);
	Message.Header.Type = EventGeneric;

	/* Setup generic message */
	Message.Type = GenericWindowQuery;
	Message.LoParam = (size_t)SharedPtr;
	Message.HiParam = 0;

	/* Send request */
	if (MollenOSMessageSend(1, &Message, Message.Header.Length)) {
		brel((void*)QueryInfo);
		return -1;
	}

	/* Wait for window manager to respond
	* but don't wait for longer than 1 sec.. */
	MollenOSSignalWait(1000);

	/* Copy information */
	memcpy(Information, QueryInfo, sizeof(IPCWindowQuery_t));

	/* Release */
	brel((void*)QueryInfo);

	/* Done */
	return 0;
}

/* UiCreateWindow 
 * Creates a window of the given
 * dimensions and flags. The returned
 * value is the id of the newly created
 * window. Returns NULL on failure */
WndHandle_t UiCreateWindow(Rect_t *Dimensions, int Flags)
{
	/* Variables */
	IPCWindowCreate_t *WndInformation;
	MEventMessageGeneric_t Message;
	WindowDescriptor_t *WndDescriptor;
	void *SharedPtr = NULL;

	/* Sanity */
	if (__UiConnected != 1)
		return NULL;

	/* Grab buffer lock */
	SpinlockAcquire(&__UiConnectionLock);

	/* Access/Setup buffer */
	WndInformation = (IPCWindowCreate_t*)bget(sizeof(IPCWindowCreate_t));
	memset(WndInformation, 0, sizeof(IPCWindowCreate_t));

	/* Release buffer lock */
	SpinlockRelease(&__UiConnectionLock);

	/* Fill in request information */
	memcpy(&WndInformation->Dimensions, Dimensions, sizeof(Rect_t));
	WndInformation->Flags = Flags;

	/* Cast to shared handle */
	SharedPtr = TO_SHARED_HANDLE(WndInformation);

	/* Setup base message */
	Message.Header.Length = sizeof(MEventMessageGeneric_t);
	Message.Header.Type = EventGeneric;
	
	/* Setup generic message */
	Message.Type = GenericWindowCreate;
	Message.LoParam = (size_t)SharedPtr;
	Message.HiParam = 0;

	/* Send request */
	if (MollenOSMessageSend(1, &Message, Message.Header.Length)) {
		brel((void*)WndInformation);
		return NULL;
	}

	/* Wait for window manager to respond 
	 * but don't wait for longer than 1 sec.. */
	MollenOSSignalWait(1000);

	/* Save return information
	 * especially the backbuffer information */
	WndDescriptor = (WindowDescriptor_t*)malloc(sizeof(WindowDescriptor_t));
	WndDescriptor->WindowId = WndInformation->WindowId;
	WndDescriptor->Backbuffer = WndInformation->Backbuffer;
	WndDescriptor->BackbufferSize = WndInformation->BackbufferSize;
	memcpy(&WndDescriptor->Dimensions, &WndInformation->ResultDimensions,
		sizeof(Rect_t));

	/* Release */
	brel((void*)WndInformation);

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
	if (MollenOSMessageSend(1, &Message, Message.Header.Length)) {
		return -1;
	}

	/* Free resources */
	free(Wnd);

	/* Done */
	return 0;
}

/* UiQueryDimensions
 * Queries the dimensions of a window
 * handle */
int UiQueryDimensions(WndHandle_t Handle, Rect_t *Dimensions)
{
	/* Cast to structure 
	 * so we can access */
	WindowDescriptor_t *Wnd =
		(WindowDescriptor_t*)Handle;

	/* Sanity */
	if (Handle == NULL
		|| Dimensions == NULL)
		return -1;

	/* Fill out information */
	memcpy(Dimensions, &Wnd->Dimensions, sizeof(Rect_t));

	/* Success! */
	return 0;
}

/* UiQueryBackbuffer
 * Queries the backbuffer handle of a window
 * that can be used for direct pixel access to it */
int UiQueryBackbuffer(WndHandle_t Handle, void **Backbuffer, size_t *BackbufferSize)
{
	/* Cast to structure
	* so we can access */
	WindowDescriptor_t *Wnd =
		(WindowDescriptor_t*)Handle;

	/* Sanity */
	if (Handle == NULL)
		return -1;

	/* Fill out information */
	if (Backbuffer != NULL)
		*Backbuffer = Wnd->Backbuffer;
	if (BackbufferSize != NULL)
		*BackbufferSize = Wnd->BackbufferSize;

	/* Success! */
	return 0;
}

/* UiInvalidateRect
 * Invalides a region of the window
 * based on relative coordinates in the window 
 * if its called with NULL as dimensions it 
 * invalidates all */
void UiInvalidateRect(WndHandle_t Handle, Rect_t *Rect)
{
	/* Variables */
	MEventMessageGeneric_t Message;
	WindowDescriptor_t *Wnd =
		(WindowDescriptor_t*)Handle;

	/* Sanity */
	if (__UiConnected != 1
		|| Handle == NULL)
		return;

	/* Setup base message */
	Message.Header.Length = sizeof(MEventMessageGeneric_t);
	Message.Header.Type = EventGeneric;

	/* Setup generic message */
	Message.Type = GenericWindowDestroy;
	Message.LoParam = (size_t)Wnd->WindowId;
	Message.HiParam = 0; /* Future:: Flags */

	/* Setup rect */
	if (Rect != NULL)
		memcpy(&Message.RcParam, Rect, sizeof(Rect_t));
	else
		memcpy(&Message.RcParam, &Wnd->Dimensions, sizeof(Rect_t));

	/* Send request */
	MollenOSMessageSend(1, &Message, Message.Header.Length);
}
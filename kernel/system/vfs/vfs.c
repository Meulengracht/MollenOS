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
* MollenOS MCore - Virtual FileSystem
* - Initialization + Event Mechanism
*/

/* Includes 
 * - System */
#include <Vfs/Vfs.h>
#include <Heap.h>
#include <Log.h>

/* Includes
 * - C-Library */
#include <ds/list.h>
#include <string.h>
#include <ctype.h>

/* Globals */
MCoreEventHandler_t *GlbVfsEventHandler = NULL;
List_t *GlbFileSystems = NULL;
List_t *GlbOpenFiles = NULL;
int GlbFileSystemId = 0;
int GlbVfsInitHasRun = 0;
int GlbVfsFileIdGen = 0;

/* Prototypes
 * Virtual FileSystem Handler */
int VfsRequestHandler(void *UserData, MCoreEvent_t *Event);

/* Prototypes 
 * - VFS Prototypes */
void VfsRegisterDisk(UUId_t DiskId);
void VfsUnregisterDisk(UUId_t DiskId, int Forced);
MCoreFileInstance_t *VfsOpen(const char *Path, VfsFileFlags_t OpenFlags);
VfsErrorCode_t VfsClose(MCoreFileInstance_t *Handle);
VfsErrorCode_t VfsDelete(MCoreFileInstance_t *Handle);
size_t VfsRead(MCoreFileInstance_t *Handle, uint8_t *Buffer, size_t Length);
size_t VfsWrite(MCoreFileInstance_t *Handle, uint8_t *Buffer, size_t Length);
VfsErrorCode_t VfsSeek(MCoreFileInstance_t *Handle, uint64_t Offset);
VfsErrorCode_t VfsFlush(MCoreFileInstance_t *Handle);

/* VfsInit
 * Initializes the virtual filesystem and 
 * all resources related, and starts the VFSEventLoop */
void VfsInit(void)
{
	/* Debug */
	LogInformation("VFSM", "Initializing");

	/* Create lists */
	GlbFileSystems = ListCreate(KeyInteger, LIST_SAFE);
	GlbOpenFiles = ListCreate(KeyInteger, LIST_SAFE);

	/* Init variables */
	GlbFileSystemId = 0;
	GlbVfsInitHasRun = 0;
	GlbVfsFileIdGen = 0;

	/* Start the event-handler */
	GlbVfsEventHandler = EventInit("Virtual FileSystem", VfsRequestHandler, NULL);
}

/* VfsRequestCreate
 * - Create a new request for the VFS */
void VfsRequestCreate(MCoreVfsRequest_t *Request)
{
	/* TODO::: Sort, Elevator Algorithm */

	/* Deep call since we have no work to 
	 * be done before hand */
	EventCreate(GlbVfsEventHandler, &Request->Base);
}

/* VfsRequestWait 
 * - Wait for a request to complete, thread 
 *   will sleep/block for the duration */
void VfsRequestWait(MCoreVfsRequest_t *Request, size_t Timeout)
{
	/* Deep call since we have no work to 
	 * be done before hand */
	EventWait(&Request->Base, Timeout);
}

/* VfsRequestHandler 
 * This is the main handler loop, this handles all
 * requests given to our system */
int VfsRequestHandler(void *UserData, MCoreEvent_t *Event)
{
	/* Vars */
	MCoreVfsRequest_t *Request;

	/* Unused */
	_CRT_UNUSED(UserData);

	/* Cast */
	Request = (MCoreVfsRequest_t*)Event;

	/* Set initial status */
	Request->Base.State = EventInProgress;

	/* Handle Event */
	switch (Request->Base.Type)
	{
		/* VFS Disk - Register Disk */
		case VfsRequestRegisterDisk:
		{
			/* Now just redirect the call to the 
			 * function, no more processing needed here */
			VfsRegisterDisk(Request->Value.Lo.DiskId);

			/* Set error to none, hopefully this never fails */
			Request->Error = VfsOk;

		} break;

		/* VFS Disk - Unregister Disk */
		case VfsRequestUnregisterDisk:
		{
			/* Now just redirect the call to the
			 * function, no more processing needed here */
			VfsUnregisterDisk(Request->Value.Lo.DiskId, Request->Value.Hi.Forced);

			/* Set error to none, hopefully this never fails */
			Request->Error = VfsOk;

		} break;

		/* VFS Function - Open File */
		case VfsRequestOpenFile:
		{
			/* Now just redirect the call to the
			* function, no more processing needed here */
			Request->Pointer.Handle =
				VfsOpen(Request->Pointer.Path, Request->Value.Lo.Flags);

			/* Redirect error */
			Request->Error = Request->Pointer.Handle->Code;

		} break;

		/* VFS Function - Close File */
		case VfsRequestCloseFile:
		{
			/* Now just redirect the call to the
			 * function, no more processing needed here */
			Request->Error = VfsClose(Request->Pointer.Handle);

		} break;

		/* VFS Function - Delete File */
		case VfsRequestDeleteFile:
		{
			/* Now just redirect the call to the
			* function, no more processing needed here */
			Request->Error = VfsDelete(Request->Pointer.Handle);

		} break;

		/* VFS Function - Read File */
		case VfsRequestReadFile:
		{
			/* Now just redirect the call to the
			* function, no more processing needed here */
			Request->Value.Hi.Length = VfsRead(Request->Pointer.Handle, 
				Request->Buffer, Request->Value.Lo.Length);

			/* Redirect error */
			Request->Error = Request->Pointer.Handle->Code;

		} break;

		/* VFS Function - Write File */
		case VfsRequestWriteFile:
		{
			/* Now just redirect the call to the
			* function, no more processing needed here */
			Request->Value.Hi.Length = VfsWrite(Request->Pointer.Handle,
				Request->Buffer, Request->Value.Lo.Length);

			/* Redirect error */
			Request->Error = Request->Pointer.Handle->Code;

		} break;

		/* VFS Function - Seek File */
		case VfsRequestSeekFile:
		{
			/* We need to build the 64 bit value */
			uint64_t AbsOffset = ((uint64_t)Request->Value.Hi.Length << 32);

			/* Build build */
			AbsOffset |= Request->Value.Lo.Length;

			/* Now just redirect the call to the
			 * function, no more processing needed here */
			Request->Error = VfsSeek(Request->Pointer.Handle, AbsOffset);

		} break;

		/* VFS Function - Flush File */
		case VfsRequestFlushFile:
		{
			/* Now just redirect the call to the
			 * function, no more processing needed here */
			Request->Error = VfsFlush(Request->Pointer.Handle);

		} break;

		default:
			break;
	}

	/* Done! */
	return 0;
}

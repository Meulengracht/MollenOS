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
#include <Servers/Vfs.h>
#include <Module.h>

/* Includes
 * - C-Library */
#include <ds/list.h>
#include <os/mollenos.h>
#include <string.h>
#include <ctype.h>

/* Globals */
List_t *GlbFileSystems = NULL;
List_t *GlbOpenFiles = NULL;
int GlbFileSystemId = 0;
int GlbVfsInitHasRun = 0;
int GlbVfsFileIdGen = 0;
int GlbVfsRun = 0;

/* Prototypes 
 * - VFS Prototypes */
void VfsRegisterDisk(DevId_t DiskId);
void VfsUnregisterDisk(DevId_t DiskId, int Forced);
MCoreFileInstance_t *VfsOpen(const char *Path, VfsFileFlags_t OpenFlags);
VfsErrorCode_t VfsClose(MCoreFileInstance_t *Handle);
VfsErrorCode_t VfsDelete(MCoreFileInstance_t *Handle);
size_t VfsRead(MCoreFileInstance_t *Handle, uint8_t *Buffer, size_t Length);
size_t VfsWrite(MCoreFileInstance_t *Handle, uint8_t *Buffer, size_t Length);
VfsErrorCode_t VfsSeek(MCoreFileInstance_t *Handle, uint64_t Offset);
VfsErrorCode_t VfsFlush(MCoreFileInstance_t *Handle);

/* Entry point of a module or server 
 * this handles setup and enters the event-queue 
 * Initializes the virtual filesystem and 
 * all resources related, and starts the VFSEventLoop */
MODULES_API void ModuleInit(void *Data)
{
	/* Save */
	_CRT_UNUSED(Data);

	/* Create lists */
	GlbFileSystems = ListCreate(KeyInteger, LIST_SAFE);
	GlbOpenFiles = ListCreate(KeyInteger, LIST_SAFE);

	/* Init variables */
	GlbFileSystemId = 0;
	GlbVfsInitHasRun = 0;
	GlbVfsFileIdGen = 0;
	GlbVfsRun = 1;

	/* Register us with server manager */

	/* Enter event queue */
	while (GlbVfsRun)
	{
		/* Storage for message */
		MCoreVfsRequest_t Request;

		/* Wait for event */
		if (!MollenOSMessageWait((MEventMessage_t*)&Request))
		{
			/* Control message or response? */
			if (Request.Base.Type == EventServerControl) 
			{
				/* Control message 
				 * Cast the type to generic */
				MEventMessageGeneric_t *ControlMsg = (MEventMessageGeneric_t*)&Request;

				/* Switch command */
				switch (ControlMsg->Type)
				{



					/* Invalid is not for us */
					default:
						break;
				}
			}
			else if (Request.Base.Type == EventServerCommand) 
			{
				/* Handle request */
				switch (Request.Type)
				{
					/* VFS Disk - Register Disk */
					case VfsRequestRegisterDisk:
					{
						/* Now just redirect the call to the
						* function, no more processing needed here */
						VfsRegisterDisk(Request.Value.Lo.DiskId);

						/* Set error to none, hopefully this never fails */
						Request.Error = VfsOk;

					} break;

					/* VFS Disk - Unregister Disk */
					case VfsRequestUnregisterDisk:
					{
						/* Now just redirect the call to the
						* function, no more processing needed here */
						VfsUnregisterDisk(Request.Value.Lo.DiskId, Request.Value.Hi.Forced);

						/* Set error to none, hopefully this never fails */
						Request.Error = VfsOk;

					} break;

					/* VFS Function - Open File */
					case VfsRequestOpenFile:
					{
						/* Now just redirect the call to the
						* function, no more processing needed here */
						Request.Pointer.Handle =
							VfsOpen(Request.Pointer.Path, Request.Value.Lo.Flags);

						/* Redirect error */
						Request.Error = Request.Pointer.Handle->Code;

					} break;

					/* VFS Function - Close File */
					case VfsRequestCloseFile:
					{
						/* Now just redirect the call to the
						* function, no more processing needed here */
						Request.Error = VfsClose(Request.Pointer.Handle);

					} break;

					/* VFS Function - Delete File */
					case VfsRequestDeleteFile:
					{
						/* Now just redirect the call to the
						* function, no more processing needed here */
						Request.Error = VfsDelete(Request.Pointer.Handle);

					} break;

					/* VFS Function - Read File */
					case VfsRequestReadFile:
					{
						/* Now just redirect the call to the
						* function, no more processing needed here */
						Request.Value.Hi.Length = VfsRead(Request.Pointer.Handle,
							Request.Buffer, Request.Value.Lo.Length);

						/* Redirect error */
						Request.Error = Request.Pointer.Handle->Code;

					} break;

					/* VFS Function - Write File */
					case VfsRequestWriteFile:
					{
						/* Now just redirect the call to the
						* function, no more processing needed here */
						Request.Value.Hi.Length = VfsWrite(Request.Pointer.Handle,
							Request.Buffer, Request.Value.Lo.Length);

						/* Redirect error */
						Request.Error = Request.Pointer.Handle->Code;

					} break;

					/* VFS Function - Seek File */
					case VfsRequestSeekFile:
					{
						/* We need to build the 64 bit value */
						uint64_t AbsOffset = ((uint64_t)Request.Value.Hi.Length << 32);

						/* Build build */
						AbsOffset |= Request.Value.Lo.Length;

						/* Now just redirect the call to the
						* function, no more processing needed here */
						Request.Error = VfsSeek(Request.Pointer.Handle, AbsOffset);

					} break;

					/* VFS Function - Flush File */
					case VfsRequestFlushFile:
					{
						/* Now just redirect the call to the
						* function, no more processing needed here */
						Request.Error = VfsFlush(Request.Pointer.Handle);

					} break;

					default:
						break;
				}

				/* Switch request to response */
				Request.Base.Type = EventServerResponse;

				/* Send structure return */
				MollenOSMessageSend(Request.Base.Sender, &Request, sizeof(MCoreVfsRequest_t));
			}
		}
		else {
			/* Wtf? */
		}
	}
}

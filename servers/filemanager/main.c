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
 * MollenOS MCore - Virtual FileSystem
 * - Initialization + Event Mechanism
 */

/* Includes
 * - System */
#include <os/driver/file.h>
#include <os/mollenos.h>
#include <ds/list.h>
#include "include/vfs.h"

/* Includes
 * - C-Library */
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <ctype.h>

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

/* Globals */
List_t *GlbFileSystems = NULL;
List_t *GlbOpenFiles = NULL;
UUId_t GlbFileSystemId = 0;
UUId_t GlbVfsFileIdGen = 0;
int GlbInitialized = 0;
int GlbRun = 0;

/* OnLoad
 * The entry-point of a server, this is called
 * as soon as the server is loaded in the system */
OsStatus_t OnLoad(void)
{
	/* Setup list */
	GlbFileSystems = ListCreate(KeyInteger, LIST_NORMAL);
	GlbOpenFiles = ListCreate(KeyInteger, LIST_NORMAL);

	/* Init variables */
	GlbFileSystemId = 0;
	GlbVfsFileIdGen = 0;
	GlbInitialized = 1;
	GlbRun = 1;

	/* Register us with server manager */
	RegisterServer(__FILEMANAGER_TARGET);

	/* No error! */
	return OsNoError;
}

/* OnUnload
 * This is called when the server is being unloaded
 * and should free all resources allocated by the system */
OsStatus_t OnUnload(void)
{
	return OsNoError;
}

/* OnEvent
 * This is called when the server recieved an external evnet
 * and should handle the given event*/
OsStatus_t OnEvent(MRemoteCall_t *Message)
{
	/* Which function is called? */
	switch (Message->Function)
	{
		/* Handles registration of a new disk 
		 * and store it with a custom version of
		 * our own filesystem classs */
		case __FILEMANAGER_REGISTERDISK: {

		} break;

		/* Unregisters a device from the system, and 
		 * signals all drivers that are attached to 
		 * un-attach */
		case __FILEMANAGER_UNREGISTERDISK: {

		} break;

		/* Queries device information and returns
		 * information about the device and the drivers
		 * attached */
		case __FILEMANAGER_OPENFILE: {

		} break;

		/* What do? */
		case __FILEMANAGER_CLOSEFILE: {

		} break;

		default: {
		} break;
	}

	/* Done! */
	return OsNoError;
}

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
 * MollenOS - File Manager Service
 * - Handles all file related services and disk services
 */

/* Includes
 * - System */
#include <os/driver/file.h>
#include <os/mollenos.h>
#include <ds/list.h>

/* Includes
 * - C-Library */
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <ctype.h>

/* Globals */
List_t *GlbFileSystems = NULL;
List_t *GlbOpenFiles = NULL;
UUId_t GlbFileSystemId = 0;
UUId_t GlbVfsFileIdGen = 0;
int GlbInitialized = 0;

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
		 * and and parses the disk-system for a MBR
		 * or a GPT table */
		case __FILEMANAGER_REGISTERDISK: {

			/* Redirect the call */
			RegisterDisk((UUId_t)Message->Arguments[0].Data.Value,
				(UUId_t)Message->Arguments[1].Data.Value,
				(Flags_t)Message->Arguments[2].Data.Value);

		} break;

		/* Unregisters a disk from the system and
		 * handles cleanup of all attached filesystems */
		case __FILEMANAGER_UNREGISTERDISK: {

			/* Redirect the call */
			UnregisterDisk((UUId_t)Message->Arguments[0].Data.Value,
				(Flags_t)Message->Arguments[1].Data.Value);

		} break;

		default: {
		} break;
	}

	/* Done! */
	return OsNoError;
}

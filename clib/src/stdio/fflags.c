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
* MollenOS C Library - File mode flags
*/

/* Includes */
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <os/Syscall.h>

/* MollenOS VFS Flags */
#define VFS_READ			0x1
#define VFS_WRITE			0x2
#define VFS_CREATE			0x4
#define VFS_TRUNCATE		0x8
#define VFS_FAILONEXISTS	0x10
#define VFS_BINARY			0x20
#define VFS_NOBUFFERING		0x40
#define VFS_APPEND			0x80

/* The freopen
* Reuses the file
* handle to either open a new
* file or switch access mode */
int fflags(const char * mode)
{
	/* Variables */
	int mFlags = 0;
	int PlusConsumed = 1;

	/* Convert mode to
	* FileFlags */
	/* Read modes first */
	if (strchr(mode, 'r') != NULL) {
		mFlags |= VFS_READ;
		if (strchr(mode, '+') != NULL) {
			mFlags |= VFS_WRITE;
			PlusConsumed = 1;
		}
	}

	/* Write modes */
	if (strchr(mode, 'w') != NULL) {
		mFlags |= VFS_WRITE | VFS_CREATE | VFS_TRUNCATE;
		if (!PlusConsumed
			&& strchr(mode, '+') != NULL) {
			mFlags |= VFS_READ;
			PlusConsumed = 1;
		}
		if (strchr(mode, 'x') != NULL) {
			mFlags |= VFS_FAILONEXISTS;
		}
	}

	/* Append modes */
	if (strchr(mode, 'a') != NULL) {
		mFlags |= VFS_APPEND | VFS_CREATE | VFS_WRITE;
		if (!PlusConsumed
			&& strchr(mode, '+') != NULL) {
			mFlags |= VFS_READ;
			PlusConsumed = 1;
		}
	}

	/* Specials */
	if (strchr(mode, 'b') != NULL) {
		mFlags |= VFS_BINARY;
	}

	/* Done */
	return mFlags;
}
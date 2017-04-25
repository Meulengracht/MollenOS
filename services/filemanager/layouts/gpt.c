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
#include "../include/vfs.h"
#include "../include/gpt.h"

/* GptEnumerate
 * Enumerates a given disk with GPT data layout
 * and automatically creates new filesystem objects */
OsStatus_t GptEnumerate(FileSystemDisk_t *Disk, BufferObject_t *Buffer)
{
	_CRT_UNUSED(Disk);
	_CRT_UNUSED(Buffer);
	return OsError;
}

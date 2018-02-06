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
 * MollenOS - C Standard Library
 * - File link implementation
 */

/* Includes
 * - System */
#include <os/file.h>
#include <stdlib.h>
#include <errno.h>
#include <stdio.h>

/* rename
 * Renames a file if the the directories/paths match. Otherwise
 * the file will be moved. */
int rename(
	_In_ __CONST char * oldname, 
	_In_ __CONST char * newname)
{
    // @todo
    return -1;
}

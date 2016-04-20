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
* MollenOS C Library - File Set Position
*/

/* Includes */
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <os/Syscall.h>

/* The fsetpos
 * Restores the file position */
int fsetpos(FILE * stream, const fpos_t * pos)
{
	/* Syscall Result */
	int RetVal = 0;

	/* Sanity */
	if (stream == NULL
		|| pos == NULL) {
		_set_errno(EINVAL);
		return -1;
	}

	/* Seek to 0 */
	RetVal = Syscall2(MOLLENOS_SYSCALL_VFSSEEK,
		MOLLENOS_SYSCALL_PARAM(stream), MOLLENOS_SYSCALL_PARAM(*pos));

	/* Sanity */
	if (stream->code == CLIB_OK_CODE)
		_set_errno(EOK);

	/* Done */
	return RetVal;
}
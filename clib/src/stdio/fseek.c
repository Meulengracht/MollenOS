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
* MollenOS C Library - File Seek
*/

/* Includes */
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <os/Syscall.h>

/* The seek
 * Set the file position */
int fseek(FILE * stream, long int offset, int origin)
{
	/* Syscall Result */
	int RetVal = 0;
	long int SeekSpot = 0;

	/* Sanity */
	if (stream == NULL) {
		_set_errno(EINVAL);
		return -1;
	}

	/* Depends on origin */
	if (origin == SEEK_SET)
		SeekSpot = offset;
	else {
		/* We need current position / size */

		/* Prepare a buffer */
		long fPos = 0, fSize = 0;
		char Buffer[64];
		memset(Buffer, 0, sizeof(Buffer));

		/* Syscall */
		RetVal = Syscall4(MOLLENOS_SYSCALL_VFSQUERY, MOLLENOS_SYSCALL_PARAM(stream),
			MOLLENOS_SYSCALL_PARAM(0), 
			MOLLENOS_SYSCALL_PARAM(&Buffer[0]), 
			MOLLENOS_SYSCALL_PARAM(sizeof(Buffer)));

		/* Now we can calculate */
		fPos = *((long*)(&Buffer[16]));
		fSize = *((long*)(&Buffer[0]));

		/* Lets see .. */
		if (origin == SEEK_CUR)
			SeekSpot = fPos + offset;
		else
			SeekSpot = fSize + offset;
	}

	/* Seek to 0 */
	RetVal = Syscall2(MOLLENOS_SYSCALL_VFSSEEK,
		MOLLENOS_SYSCALL_PARAM(stream), MOLLENOS_SYSCALL_PARAM(SeekSpot));

	/* Done */
	return RetVal;
}
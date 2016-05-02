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

/* The seeko
 * Set the file position with off_t */
int fseeko(FILE *stream, off_t offset, int origin)
{
	/* Syscall Result */
	int RetVal = 0;
	off_t SeekSpot = 0;

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
		uint64_t fPos = 0, fSize = 0;
		size_t CorrectedValue = (size_t)abs(offset);;
		char Buffer[64];
		memset(Buffer, 0, sizeof(Buffer));

		/* Syscall */
		RetVal = Syscall4(MOLLENOS_SYSCALL_VFSQUERY, MOLLENOS_SYSCALL_PARAM(stream),
			MOLLENOS_SYSCALL_PARAM(0), 
			MOLLENOS_SYSCALL_PARAM(&Buffer[0]), 
			MOLLENOS_SYSCALL_PARAM(sizeof(Buffer)));

		/* Now we can calculate */
		fPos = *((uint64_t*)(&Buffer[16]));
		fSize = *((uint64_t*)(&Buffer[0]));

		/* Sanity offset */
		if ((size_t)fPos != fPos) {
			errno = EOVERFLOW;
			return -1;
		}

		/* Lets see .. */
		if (origin == SEEK_CUR) {
			/* Handle negative */
			if (offset < 0) {
				SeekSpot = (off_t)fPos - CorrectedValue;
			}
			else {
				SeekSpot = (off_t)fPos + CorrectedValue;
			}
		}
		else {
			SeekSpot = (off_t)fSize - CorrectedValue;
		}
	}

	/* Seek to 0 */
	RetVal = Syscall2(MOLLENOS_SYSCALL_VFSSEEK,
		MOLLENOS_SYSCALL_PARAM(stream), MOLLENOS_SYSCALL_PARAM(SeekSpot));

	/* Sanity */
	if (stream->code == CLIB_OK_CODE)
		_set_errno(EOK);

	/* Done */
	return RetVal;
}

/* The seek
 * Set the file position */
int fseek(FILE * stream, long int offset, int origin)
{
	/* Deep call */
	return fseeko(stream, offset, origin);
}
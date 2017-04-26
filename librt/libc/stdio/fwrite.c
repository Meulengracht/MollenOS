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
 * MollenOS C Library - Write to file-handles
 */

/* Includes
 * - System */
#include <os/driver/file.h>
#include <os/syscall.h>
#include <os/thread.h>

/* Includes 
 * - Library */
#include <io.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>

/* Externs */
__EXTERN int _favail(FILE * stream);

/* _write
 * This is the ANSI C version of fwrite */
int _write(int fd, void *buffer, unsigned int length)
{
	/* Variables */
	size_t BytesWrittenTotal = 0, BytesLeft = (size_t)length;
	size_t OriginalSize = GetBufferSize(TLSGetCurrent()->Transfer);
	uint8_t *Pointer = (uint8_t*)buffer;

	/* Keep reading chunks of BUFSIZ */
	while (BytesLeft > 0) {
		size_t ChunkSize = MIN(OriginalSize, BytesLeft);
		size_t BytesWritten = 0;
		ChangeBufferSize(TLSGetCurrent()->Transfer, ChunkSize);
		WriteBuffer(TLSGetCurrent()->Transfer, (__CONST void*)Pointer, ChunkSize, &BytesWritten);
		if (WriteFile((UUId_t)fd, TLSGetCurrent()->Transfer, &BytesWritten) != FsOk) {
			break;
		}
		if (BytesWritten == 0) {
			break;
		}
		BytesWrittenTotal += BytesWritten;
		BytesLeft -= BytesWritten;
		Pointer += BytesWritten;
	}

	/* Done! */
	ChangeBufferSize(TLSGetCurrent()->Transfer, OriginalSize);
	return (int)BytesWrittenTotal;
}

/* The fwrite
* writes to a file handle */
size_t fwrite(const void * vptr, size_t size, size_t count, FILE * stream)
{
	/* Variables */
	size_t BytesToWrite = count * size;

	/* Sanity */
	if (vptr == NULL
		|| stream == NULL
		|| stream == stderr
		|| stream == stdout
		|| stream == stdin
		|| BytesToWrite == 0)
		return 0;

	/* Sanity, if we are in position of a 
	 * buffer, we need to adjust before writing */
	if (_favail(stream)) {
		fseek(stream, 0 - _favail(stream), SEEK_CUR);
	}

	/* Write to file */
	_set_errno(EOK);
	return (size_t)_write((int)stream->fd, (void*)vptr, BytesToWrite);
}

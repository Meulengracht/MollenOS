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
* MollenOS C Library - File Fill Buffer
*/

/* Includes */
#include <io.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <os/Syscall.h>

/* Private structure 
 * it's not exposed for users */
typedef struct _CLibraryFileBuffer {

	/* Buffer 
	 * The actual buffer */
	void *Buffer;

	/* The buffer pointer */
	int Pointer;

	/* The buffer size 
	 * so we know the limit */
	int Size;

	/* Cleanup ? */
	int Cleanup;

} FILEBUFFER, *PFILEBUFFER;

/* The _ffill
 * fills a file buffer */
int _ffill(FILE * stream, void *ptr, size_t size)
{
	/* Variables */
	FILEBUFFER *fbuffer;
	size_t bavail = 0;
	int bread = 0;

	/* First of all
	 * do we have the structure? */
	if (stream->buffer == NULL) 
	{
		/* Allocate a new structure */
		stream->buffer = malloc(sizeof(FILEBUFFER));

		/* Cast */
		fbuffer = (FILEBUFFER*)stream->buffer;

		/* Reset buffer */
		fbuffer->Buffer = malloc(BUFSIZ);
		fbuffer->Pointer = -1;
		fbuffer->Cleanup = 1;
		fbuffer->Size = BUFSIZ;
	}
	else {
		/* Cast */
		fbuffer = (FILEBUFFER*)stream->buffer;
	}

	/* Take care of a new buffer */
	if (fbuffer->Pointer == -1
		|| (fbuffer->Pointer == fbuffer->Size)) {
		/* Read a complete block */
		int errcode = 0;
		int retval = Syscall4(MOLLENOS_SYSCALL_VFSREAD, MOLLENOS_SYSCALL_PARAM(stream->fd),
			MOLLENOS_SYSCALL_PARAM(fbuffer->Buffer), MOLLENOS_SYSCALL_PARAM(fbuffer->Size),
			MOLLENOS_SYSCALL_PARAM(&errcode));

		/* Sanity */
		if (_fval(errcode)) {
			return -1;
		}

		/* If we read 0 bytes and errcode is ok,
		 * then we are EOF */
		if (retval == 0 && errcode == 0) 
		{
			/* Set status code, 
			 * invalidate buffer */
			stream->code |= CLIB_FCODE_EOF;
			fbuffer->Pointer = fbuffer->Size;

			/* Return 0 */
			return 0;
		}

		/* Update pointer */
		fbuffer->Pointer = 0;
	}

	/* How many bytes are available? */
	bavail = fbuffer->Size - fbuffer->Pointer;
	
	/* Set bread */
	bread = (int)MIN(bavail, size);

	/* Copy over */
	memcpy(ptr, (void*)((uint8_t*)fbuffer->Buffer + fbuffer->Pointer), bread);

	/* Increase the pointer */
	fbuffer->Pointer += bread;

	/* Done! */
	return bread;
}

/* _finv 
 * invalidates the current buffer */
int _finv(FILE * stream)
{
	/* Variables */
	FILEBUFFER *fbuffer;

	/* Sanity */
	if (stream == NULL)
		return -1;

	/* Make sure the buffer is 
	 * allocated before invalidation */
	if (stream->buffer != NULL) 
	{
		/* Cast */
		fbuffer = (FILEBUFFER*)stream->buffer;

		/* Invalidate the pointer */
		fbuffer->Pointer = -1;
	}

	/* Done! */
	return 0;
}

/* _favail
 * returns the number of bytes left in the buffer */
int _favail(FILE * stream)
{
	/* Variables */
	FILEBUFFER *fbuffer;

	/* Sanity */
	if (stream == NULL)
		return 0;

	/* Make sure the buffer is
	 * allocated before invalidation */
	if (stream->buffer != NULL)
	{
		/* Cast */
		fbuffer = (FILEBUFFER*)stream->buffer;

		/* Return bytes available */
		if (fbuffer->Pointer == -1)
			return 0;
		else
			return (fbuffer->Size - fbuffer->Pointer);
	}

	/* Done! */
	return 0;
}

/* The _ffillclean
 * cleans up the file buffer */
int _ffillclean(FILE * stream)
{
	/* Variables */
	FILEBUFFER *fbuffer;

	/* Sanity */
	if (stream == NULL)
		return 0;

	/* Make sure the buffer is
	* allocated before invalidation */
	if (stream->buffer != NULL)
	{
		/* Cast */
		fbuffer = (FILEBUFFER*)stream->buffer;

		/* Cleanup buffer */
		if (fbuffer->Cleanup == 1)
			free(fbuffer->Buffer);
		
		/* Clean structure */
		free(fbuffer);

		/* Invalidate */
		stream->buffer = NULL;
	}

	/* Done! */
	return 0;
}

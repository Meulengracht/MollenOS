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
 * MollenOS C Library - Internal file-buffers
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

/* Private structure 
 * it's not exposed for users */
typedef struct _CFILEBUFFER {
	void					*Buffer;
	int						 Pointer;
	int						 Size;
	int						 TempSize;
	int						 Cleanup;
} FILEBUFFER, *PFILEBUFFER;

/* _ffullread
 * Reads not as a chunk, but as one entire transaction
 * for speedup */
int _ffullread(FILE * stream, void *ptr, size_t len)
{
	/* Variables */
	BufferObject_t *Buffer = NULL;
	size_t BytesRead = 0;

	/* Create a new buffer for transfer */
	Buffer = CreateBuffer(len);

	/* Do the read */
	_fval((int)ReadFile(stream->fd, Buffer, &BytesRead));
	if (BytesRead != 0) {
		ReadBuffer(Buffer, ptr, BytesRead);
	}

	/* Cleanup buffers */
	DestroyBuffer(Buffer);
	return BytesRead;
}

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
		fbuffer->TempSize = 0;
	}
	else {
		/* Cast */
		fbuffer = (FILEBUFFER*)stream->buffer;
	}

	/* Sanitize whther or not this file-stream has
	 * stream-buffering disabled */
	if (stream->code & _IONBF) {
		int retval = _ffullread(stream, ptr, size);

		/* Sanity */
		if (errno != EOK) {
			return -1;
		}
		else {
			return retval;
		}
	}

	/* Take care of a new buffer 
	 * but check if we are at end of buffer
	 * or buffer has never been used */
	if (fbuffer->Pointer == -1
		|| (fbuffer->Pointer == fbuffer->TempSize)) {
		/* Read a complete block */
		int retval = 0;

		/* Determine if we read more than a full buffer */
		if (size >= (size_t)fbuffer->Size) {
			retval = _ffullread(stream, ptr, size);
		}
		else {
			retval = _read(stream->fd, fbuffer->Buffer, fbuffer->Size);
		}

		/* Sanity */
		if (errno != EOK) {
			return -1;
		}

		/* If we read 0 bytes and errcode is ok,
		 * then we are EOF */
		if (retval == 0 && errno == EOK)
		{
			/* Set status code, 
			 * invalidate buffer */
			stream->code |= _IOEOF;
			fbuffer->Pointer = 0;
			fbuffer->TempSize = 0;

			/* Return 0 */
			return 0;
		}

		/* Sanity, if we read more than buffer 
		 * we are still invalid */
		if (size >= (size_t)fbuffer->Size) {
			/* Invalidate */
			fbuffer->Pointer = -1;

			/* Return number of bytes read */
			return retval;
		}
		else {
			/* Update temporary size */
			fbuffer->TempSize = retval;

			/* Update pointer */
			fbuffer->Pointer = 0;
		}
	}

	/* How many bytes are available? */
	bavail = fbuffer->TempSize - fbuffer->Pointer;
	
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
			return (fbuffer->TempSize - fbuffer->Pointer);
	}

	/* Done! */
	return 0;
}

/* _fbufptr
 * returns the buffer pointer position */
int _fbufptr(FILE * stream)
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

		/* Return bytes available */
		return fbuffer->Pointer;
	}

	/* Done! */
	return -1;
}

/* _fbufadjust
 * Adjusts the buffer pointer by an offset
 * used for internal buf seeking */
int _fbufadjust(FILE * stream, off_t offset)
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

		/* Adjust and return */
		fbuffer->Pointer += offset;

		/* Done */
		return 0;
	}

	/* Done! */
	return -1;
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

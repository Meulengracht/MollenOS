/* MollenOS
*
* Copyright 2011 - 2014, Philip Meulengracht
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
* MollenOS Common - Ring Buffer Implementation
*/

#ifndef _MCORE_RINGBUFFER_H_
#define _MCORE_RINGBUFFER_H_

/* Includes */
#include <arch.h>
#include <crtdefs.h>
#include <stdint.h>

/* Structure */
typedef struct _ringbuffer
{
	/* The buffer */
	uint8_t *buffer;

	/* Length of buffer */
	size_t length;

	/* Index */
	uint32_t index_write;
	uint32_t index_read;

	/* Lock */
	spinlock_t lock;

} ringbuffer_t;

/* Prototypes */

/* Initialise a new ring buffer */
_CRT_EXTERN ringbuffer_t *ringbuffer_create(size_t size);

/* Destroy Ringbuffer */
_CRT_EXTERN void ringbuffer_destroy(ringbuffer_t *ringbuffer);

/* Write to buffer */
_CRT_EXTERN int ringbuffer_write(ringbuffer_t *ringbuffer, size_t size, uint8_t *buffer);

/* Read from buffer */
_CRT_EXTERN int ringbuffer_read(ringbuffer_t *ringbuffer, size_t size, uint8_t *buffer);

/* How many bytes are available in buffer */
_CRT_EXTERN size_t ringbuffer_size(ringbuffer_t *ringbuffer);

#endif // !_MCORE_RINGBUFFER_H_

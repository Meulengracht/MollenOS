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

#include <ringbuffer.h>
#include <heap.h>
#include <stddef.h>

/* Initialise a new ring buffer */
ringbuffer_t *ringbuffer_create(size_t size)
{
	/* Allocate */
	ringbuffer_t *ringbuffer = (ringbuffer_t*)kmalloc(sizeof(ringbuffer_t));
	ringbuffer->buffer = (uint8_t*)kmalloc(size);
	
	/* Setup rest */
	ringbuffer->index_write = 0;
	ringbuffer->index_read = 0;
	ringbuffer->length = size;

	/* Reset lock */
	spinlock_reset(&ringbuffer->lock);

	return ringbuffer;
}

/* Destroy Ringbuffer */
void ringbuffer_destroy(ringbuffer_t *ringbuffer)
{
	/* Free stuff */
	kfree(ringbuffer->buffer);
	kfree(ringbuffer);
}

/* How many bytes are ready for usage */
int ringbuffer_data_available(ringbuffer_t *ringbuffer)
{
	/* If read_index == write_index then we have no of data ready hihi */
	if (ringbuffer->index_read == ringbuffer->index_write)
		return (int)(ringbuffer->length - 1);

	/* If read index is higher than write, we have wrapped around */
	if (ringbuffer->index_read > ringbuffer->index_write)
		return (int)(ringbuffer->index_read - ringbuffer->index_write - 1);

	/* Otherwise we haven't wrapped, just return difference */
	return (int)((ringbuffer->length - ringbuffer->index_write) + ringbuffer->index_read - 1);
}

/* How many bytes are ready to be read */
size_t ringbuffer_size(ringbuffer_t *ringbuffer)
{
	/* Check if they are equal */
	if (ringbuffer->index_read == ringbuffer->index_write)
		return 0;

	/* If read index is above write, it has wrapped */
	if (ringbuffer->index_read > ringbuffer->index_write)
		return ((ringbuffer->length - ringbuffer->index_read) + ringbuffer->index_write);

	/* Else its simple, subtract */
	return (ringbuffer->index_write - ringbuffer->index_read);
}

/* Write to buffer */
int ringbuffer_write(ringbuffer_t *ringbuffer, size_t size, uint8_t *buffer)
{
	size_t bytes_written = 0;
	interrupt_status_t int_state;

	/* Sanity */
	if (ringbuffer == NULL)
		return -1;

	/* Acquire lock */
	int_state = interrupt_disable();
	spinlock_acquire(&ringbuffer->lock);

	/* Only write while buffer is available */
	while (1)
	{
		while (ringbuffer_data_available(ringbuffer) > 0 && bytes_written < size)
		{
			/* Write byte */
			ringbuffer->buffer[ringbuffer->index_write] = buffer[bytes_written];

			/* Increase */
			ringbuffer->index_write++;

			/* Do we have to wrap around? */
			if (ringbuffer->index_write == ringbuffer->length)
				ringbuffer->index_write = 0;

			bytes_written++;
		}

		/* Did we write all data? :/ */
		if (bytes_written < size)
		{
			/* No, sleep time :( */

			/* Release lock */
			spinlock_release(&ringbuffer->lock);
			interrupt_set_state(int_state);
		}
		else
			break;
	}

	/* Release lock */
	spinlock_release(&ringbuffer->lock);
	interrupt_set_state(int_state);

	/* Done */
	return 0;
}

/* Read data from buffer */
int ringbuffer_read(ringbuffer_t *ringbuffer, size_t size, uint8_t *buffer)
{
	size_t bytes_read = 0;
	interrupt_status_t int_state;

	/* Sanity */
	if (ringbuffer == NULL)
		return -1;

	/* Acquire lock */
	int_state = interrupt_disable();
	spinlock_acquire(&ringbuffer->lock);

	while (1)
	{
		while (ringbuffer_size(ringbuffer) > 0 && bytes_read < size)
		{
			/* Read */
			buffer[bytes_read] = ringbuffer->buffer[ringbuffer->index_read];

			/* Increament */
			ringbuffer->index_read++;

			/* Do we have to wrap around? */
			if (ringbuffer->index_write == ringbuffer->length)
				ringbuffer->index_write = 0;

			bytes_read++;
		}

		/* Did we write all data? :/ */
		if (bytes_read < size)
		{
			/* No, sleep time :( */

			/* Release lock */
			spinlock_release(&ringbuffer->lock);
			interrupt_set_state(int_state);
		}
		else
			break;
	}
	
	/* Release lock */
	spinlock_release(&ringbuffer->lock);
	interrupt_set_state(int_state);

	/* Done */
	return 0;
}
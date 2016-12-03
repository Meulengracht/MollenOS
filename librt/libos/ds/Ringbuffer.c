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
* MollenOS Common - Ring Buffer Implementation
*/

#include <ds/ringbuffer.h>
#include <stddef.h>

/* Initialise a new ring Buffer */
RingBuffer_t *RingBufferCreate(size_t Size)
{
	/* Allocate */
	RingBuffer_t *RingBuffer = (RingBuffer_t*)dsalloc(sizeof(RingBuffer_t));
	RingBuffer->Buffer = (uint8_t*)dsalloc(Size);
	
	/* Setup rest */
	RingBuffer->IndexWrite = 0;
	RingBuffer->IndexRead = 0;
	RingBuffer->Length = Size;

	/* Reset Lock */
	SpinlockReset(&RingBuffer->Lock);

	return RingBuffer;
}

/* Construct Ringbuffer */
void RingBufferConstruct(RingBuffer_t *RingBuffer, uint8_t *Buffer, size_t BufferLength)
{
	/* Set buffer */
	RingBuffer->Buffer = Buffer;

	/* Setup rest */
	RingBuffer->IndexWrite = 0;
	RingBuffer->IndexRead = 0;
	RingBuffer->Length = BufferLength;

	/* Reset Lock */
	SpinlockReset(&RingBuffer->Lock);
}

/* Destroy RingBuffer */
void RingBufferDestroy(RingBuffer_t *RingBuffer)
{
	/* Free stuff */
	dsfree(RingBuffer->Buffer);
	dsfree(RingBuffer);
}

/* Helpers */
void RingBufferIncreaseWrite(RingBuffer_t *RingBuffer)
{
	/* Increase */
	RingBuffer->IndexWrite++;

	/* Do we have to wrap around? */
	if (RingBuffer->IndexWrite == (int)RingBuffer->Length)
		RingBuffer->IndexWrite = 0;
}

void RingBufferIncreaseRead(RingBuffer_t *RingBuffer)
{
	/* Increament */
	RingBuffer->IndexRead++;

	/* Do we have to wrap around? */
	if (RingBuffer->IndexRead == (int)RingBuffer->Length)
		RingBuffer->IndexRead = 0;
}

/* How many bytes are ready for usage */
int RingBufferSpaceAvailable(RingBuffer_t *RingBuffer)
{
	/* If read_index == write_index then we have no of data ready hihi */
	if (RingBuffer->IndexRead == RingBuffer->IndexWrite)
		return (int)(RingBuffer->Length - 1);

	/* If read index is higher than write, we have wrapped around */
	if (RingBuffer->IndexRead > RingBuffer->IndexWrite)
		return (int)(RingBuffer->IndexRead - RingBuffer->IndexWrite - 1);

	/* Otherwise we haven't wrapped, just return difference */
	return (int)((RingBuffer->Length - RingBuffer->IndexWrite) + RingBuffer->IndexRead - 1);
}

/* How many bytes are ready to be read */
size_t RingBufferSize(RingBuffer_t *RingBuffer)
{
	/* Check if they are equal */
	if (RingBuffer->IndexRead == RingBuffer->IndexWrite)
		return 0;

	/* If read index is above write, it has wrapped */
	if (RingBuffer->IndexRead > RingBuffer->IndexWrite)
		return ((RingBuffer->Length - RingBuffer->IndexRead) + RingBuffer->IndexWrite);

	/* Else its simple, subtract */
	return (RingBuffer->IndexWrite - RingBuffer->IndexRead);
}

/* Write to Buffer */
int RingBufferWrite(RingBuffer_t *RingBuffer, size_t SrcLength, uint8_t *Source)
{
	size_t BytesWritten = 0;
	int Run = 1;

	/* Sanity */
	if (RingBuffer == NULL)
		return -1;

	/* Only write while Buffer is available */
	while (Run)
	{
		/* Acquire Lock */
		SpinlockAcquire(&RingBuffer->Lock);

		while (RingBufferSpaceAvailable(RingBuffer) > 0 && BytesWritten < SrcLength)
		{
			/* Write byte */
			RingBuffer->Buffer[RingBuffer->IndexWrite] = Source[BytesWritten];

			/* Increase */
			RingBufferIncreaseWrite(RingBuffer);

			/* Increase */
			BytesWritten++;
		}

		/* Release Lock */
		SpinlockRelease(&RingBuffer->Lock);

		/* Break (for now) */
		break;
	}

	/* Done */
	return (int)BytesWritten;
}

/* Read data from Buffer */
int RingBufferRead(RingBuffer_t *RingBuffer, size_t DestLength, uint8_t *Destination)
{
	size_t BytesRead = 0; 
	int Run = 1;

	/* Sanity */
	if (RingBuffer == NULL)
		return -1;

	while (Run)
	{
		/* Acquire Lock */
		SpinlockAcquire(&RingBuffer->Lock);

		while (RingBufferSize(RingBuffer) > 0 && BytesRead < DestLength)
		{
			/* Read */
			Destination[BytesRead] = RingBuffer->Buffer[RingBuffer->IndexRead];

			/* Increament */
			RingBufferIncreaseRead(RingBuffer);

			/* Incr */
			BytesRead++;
		}
		
		/* Release Lock */
		SpinlockRelease(&RingBuffer->Lock);

		/* Break */
		break;
	}

	/* Done */
	return (int)BytesRead;
}
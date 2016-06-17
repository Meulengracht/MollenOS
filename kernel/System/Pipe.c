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
* MollenOS Pipe Interface
* Builds on principle of a ringbuffer
*/

/* Includes */
#include <Scheduler.h>
#include <Pipe.h>
#include <Heap.h>

/* C-Library */
#include <stddef.h>

/* Initialise a new pipe */
MCorePipe_t *PipeCreate(size_t Size)
{
	/* Allocate */
	MCorePipe_t *Pipe = (MCorePipe_t*)kmalloc(sizeof(MCorePipe_t));
	Pipe->Buffer = (uint8_t*)kmalloc(Size);

	/* Setup rest */
	Pipe->ReadQueueCount = 0;
	Pipe->WriteQueueCount = 0;
	Pipe->IndexWrite = 0;
	Pipe->IndexRead = 0;
	Pipe->Length = Size;

	/* Reset Lock */
	SpinlockReset(&Pipe->Lock);

	/* Setup Semaphores */
	SemaphoreConstruct(&Pipe->ReadQueue, 0);
	SemaphoreConstruct(&Pipe->WriteQueue, 0);

	/* Done */
	return Pipe;
}

/* Construct Pipe */
void PipeConstruct(MCorePipe_t *Pipe, uint8_t *Buffer, size_t BufferLength)
{
	/* Set buffer */
	Pipe->Buffer = Buffer;

	/* Setup rest */
	Pipe->IndexWrite = 0;
	Pipe->IndexRead = 0;
	Pipe->Length = BufferLength;

	/* Reset Lock */
	SpinlockReset(&Pipe->Lock);

	/* Setup Semaphores */
	SemaphoreConstruct(&Pipe->ReadQueue, 0);
	SemaphoreConstruct(&Pipe->WriteQueue, 0);
}

/* Destroy Pipe */
void PipeDestroy(MCorePipe_t *Pipe)
{
	/* Awake all */
	SchedulerWakeupAllThreads((Addr_t*)&Pipe->ReadQueue);
	SchedulerWakeupAllThreads((Addr_t*)&Pipe->WriteQueue);

	/* Free stuff */
	kfree(Pipe->Buffer);
	kfree(Pipe);
}

/* Helpers */
void PipeIncreaseWrite(MCorePipe_t *Pipe)
{
	/* Increase */
	Pipe->IndexWrite++;

	/* Do we have to wrap around? */
	if (Pipe->IndexWrite == Pipe->Length)
		Pipe->IndexWrite = 0;
}

void PipeIncreaseRead(MCorePipe_t *Pipe)
{
	/* Increament */
	Pipe->IndexRead++;

	/* Do we have to wrap around? */
	if (Pipe->IndexRead == Pipe->Length)
		Pipe->IndexRead = 0;
}

/* Write to buffer */
int PipeWrite(MCorePipe_t *Pipe, size_t SrcLength, uint8_t *Source)
{
	/* Sanity */
	if (Pipe == NULL
		|| SrcLength == 0
		|| Source == NULL)
		return -1;

	/* Write the data */
	size_t BytesWritten = 0;
	while (BytesWritten < SrcLength)
	{
		/* Lock */
		SpinlockAcquire(&Pipe->Lock);

		/* Write while there are bytes left, and space in pipe */
		while (PipeBytesLeft(Pipe) > 0 && BytesWritten < SrcLength)
		{
			/* Write byte */
			Pipe->Buffer[Pipe->IndexWrite] = Source[BytesWritten];

			/* Increase */
			PipeIncreaseWrite(Pipe);

			/* Increase aswell */
			BytesWritten++;
		}

		/* Unlock */
		SpinlockRelease(&Pipe->Lock);

		/* Wakeup one of the readers */
		if (Pipe->ReadQueueCount > 0) {
			SemaphoreV(&Pipe->ReadQueue);
			Pipe->ReadQueueCount--;
		}

		/* Do we stil need to write more? */
		if (BytesWritten < SrcLength) {
			Pipe->WriteQueueCount++;
			SemaphoreP(&Pipe->WriteQueue, 0);
		}
	}

	/* Done! */
	return (int)BytesWritten;
}

/* Read from buffer */
int PipeRead(MCorePipe_t *Pipe, size_t DestLength, uint8_t *Destination, int Peek)
{
	/* Sanity */
	if (Pipe == NULL
		|| DestLength == 0
		|| Destination == NULL)
		return -1;

	/* Read the data */
	ssize_t SavedIndex = Pipe->IndexRead;
	size_t BytesRead = 0;
	while (BytesRead == 0)
	{
		/* Lock */
		SpinlockAcquire(&Pipe->Lock);

		/* Only read while there is data available */
		while (PipeBytesAvailable(Pipe) > 0 && BytesRead < DestLength)
		{
			/* Read */
			Destination[BytesRead] = Pipe->Buffer[Pipe->IndexRead];

			/* Increase Pipe */
			PipeIncreaseRead(Pipe);

			/* Increase */
			BytesRead++;
		}

		/* Restore index if we peeked */
		if (Peek)
			Pipe->IndexRead = SavedIndex;

		/* Unlock */
		SpinlockRelease(&Pipe->Lock);

		/* Only do this part if we are not peeking */
		if (!Peek)
		{
			/* Wakeup one of the writers */
			if (Pipe->WriteQueueCount > 0) {
				SemaphoreP(&Pipe->WriteQueue, 0);
				Pipe->WriteQueueCount--;
			}

			/* Was there no data to read? */
			if (BytesRead == 0) {
				Pipe->ReadQueueCount++;
				SemaphoreV(&Pipe->ReadQueue);
			}
		}
	}

	/* Done! */
	return (int)BytesRead;
}

/* How many bytes are available in buffer to be read */
int PipeBytesAvailable(MCorePipe_t *Pipe)
{
	/* If they are in matching positions, no data */
	if (Pipe->IndexRead == Pipe->IndexWrite)
		return 0;

	/* If the read index is larger than write, */
	if (Pipe->IndexRead > Pipe->IndexWrite)
		return (int)(Pipe->Length - Pipe->IndexRead) + Pipe->IndexWrite;
	else
		return (int)(Pipe->IndexWrite - Pipe->IndexRead);
}

/* How many bytes are ready for usage */
int PipeBytesLeft(MCorePipe_t *Pipe)
{
	/* If read_index == write_index then we have no of data ready */
	if (Pipe->IndexRead == Pipe->IndexWrite)
		return (int)(Pipe->Length - 1);

	/* If read index is higher than write, we have wrapped around */
	if (Pipe->IndexRead > Pipe->IndexWrite)
		return (int)(Pipe->IndexRead - Pipe->IndexWrite - 1);

	/* Otherwise we haven't wrapped, just return difference */
	return (int)((Pipe->Length - Pipe->IndexWrite) + Pipe->IndexRead - 1);
}
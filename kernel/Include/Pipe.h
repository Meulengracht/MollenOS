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

#ifndef _MCORE_PIPE_H_
#define _MCORE_PIPE_H_

/* Includes */
#include <Arch.h>
#include <Semaphore.h>

/* CLib */
#include <crtdefs.h>
#include <stdint.h>

#define PIPE_INSECURE	0x1

/* Structure */
typedef struct _MCorePipe
{
	/* The buffer */
	uint8_t *Buffer;

	/* Length of buffer */
	size_t Length;
	Flags_t Flags;

	/* Index */
	size_t IndexWrite;
	size_t IndexRead;

	/* Primitive Lock */
	Spinlock_t Lock;

	/* Semaphores */
	Semaphore_t ReadQueue;
	Semaphore_t WriteQueue;

	/* Sleep Counters */
	size_t ReadQueueCount;
	size_t WriteQueueCount;

} MCorePipe_t;

/* Initialise a new pipe */
__CRT_EXTERN MCorePipe_t *PipeCreate(size_t Size, Flags_t Flags);

/* Construct a new pipe */
__CRT_EXTERN void PipeConstruct(MCorePipe_t *Pipe, uint8_t *Buffer, size_t BufferLength, Flags_t Flags);

/* Destroy Pipe */
__CRT_EXTERN void PipeDestroy(MCorePipe_t *Pipe);

/* Write to buffer */
__CRT_EXTERN int PipeWrite(MCorePipe_t *Pipe, size_t SrcLength, uint8_t *Source);

/* Read from buffer */
__CRT_EXTERN int PipeRead(MCorePipe_t *Pipe, size_t DestLength, uint8_t *Destination, int Peek);

/* How many bytes are available in buffer to be read */
__CRT_EXTERN int PipeBytesAvailable(MCorePipe_t *Pipe);

/* How many bytes are ready for usage */
__CRT_EXTERN int PipeBytesLeft(MCorePipe_t *Pipe);

#endif // !_MCORE_PIPE_H_

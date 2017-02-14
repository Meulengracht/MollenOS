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
 * MollenOS Pipe Interface
 *  - Builds on principle of a ringbuffer with added features
 *    as queues and locks
 */

#ifndef _MCORE_PIPE_H_
#define _MCORE_PIPE_H_

/* Includes
 * - Library */
#include <os/osdefs.h>

/* Includes 
 * - System */
#include <Semaphore.h>

/* Customization of the pipe, these are some default
 * parameters for creation */
#define PIPE_DEFAULT_SIZE			0x1000

/* Customization of the pipe, we allow sleeping
 * behaviour when waiting for data, or we can
 * return with incomplete data */
#define PIPE_NOBLOCK_READ			0x1
#define PIPE_NOBLOCK_WRITE			0x2
#define PIPE_NOBLOCK				(PIPE_NOBLOCK_READ | PIPE_NOBLOCK_WRITE)

/* The pipe structure, it basically contains
 * what equals to a ringbuffer, except it adds
 * read/write queues in order to block users */
typedef struct _MCorePipe {
	Flags_t					Flags;
	uint8_t					*Buffer;
	size_t					Length;
	size_t					IndexWrite;
	size_t					IndexRead;
	Spinlock_t				Lock;
	Semaphore_t				ReadQueue;
	Semaphore_t				WriteQueue;
	size_t					ReadQueueCount;
	size_t					WriteQueueCount;
} MCorePipe_t;

/* PipeCreate
 * Initialise a new pipe of the given size 
 * and with the given flags */
__EXTERN MCorePipe_t *PipeCreate(size_t Size, Flags_t Flags);

/* PipeConstruct
 * Construct an already existing pipe by resetting the
 * pipe with the given parameters */
__EXTERN void PipeConstruct(MCorePipe_t *Pipe, 
	uint8_t *Buffer, size_t BufferLength, Flags_t Flags);

/* PipeDestroy
 * Destroys a pipe and wakes up all sleeping threads, then
 * frees all resources allocated */
__EXTERN void PipeDestroy(MCorePipe_t *Pipe);

/* PipeWrite
 * Writes the given data to the pipe-buffer, unless PIPE_NOBLOCK_WRITE
 * has been specified, it will block untill there is room in the pipe */
__EXTERN int PipeWrite(MCorePipe_t *Pipe, uint8_t *Data, size_t Length);

/* PipeRead
 * Reads the requested data-length from the pipe buffer, unless PIPE_NOBLOCK_READ
 * has been specified, it will block untill data becomes available. If NULL is
 * given as the buffer it will just consume data instead */
__EXTERN int PipeRead(MCorePipe_t *Pipe, 
	uint8_t *Buffer, size_t Length, int Peek);

/* PipeBytesAvailable
 * Returns how many bytes are available in buffer to be read */
__EXTERN int PipeBytesAvailable(MCorePipe_t *Pipe);

/* PipeBytesLeft
 * Returns how many bytes are ready for usage/able to be written */
__EXTERN int PipeBytesLeft(MCorePipe_t *Pipe);

#endif // !_MCORE_PIPE_H_

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
#include <semaphore.h>
#include <mutex.h>

/* Customization of the pipe, these are some default
 * parameters for creation */
#define PIPE_RPCOUT_SIZE            0x1000

/* MCorePipe
 * The pipe structure, it basically contains
 * what equals to a ringbuffer, except it adds
 * read/write queues in order to block users */
typedef struct _MCorePipe {
    Flags_t                     Flags;
    uint8_t                    *Buffer;
    size_t                      Length;
    size_t                      IndexWrite;
    size_t                      IndexRead;

    Semaphore_t                 ReadQueue;
    Semaphore_t                 WriteQueue;
    size_t                      ReadQueueCount;
    size_t                      WriteQueueCount;
} MCorePipe_t;

/* PipeCreate
 * Initialise a new pipe of the given size and with the given flags */
KERNELAPI
MCorePipe_t*
KERNELABI
PipeCreate(
    _In_ size_t         Size, 
    _In_ Flags_t        Flags);

/* PipeConstruct
 * Construct an already existing pipe by resetting the
 * pipe with the given parameters */
KERNELAPI
void
KERNELABI
PipeConstruct(
    _In_ MCorePipe_t    *Pipe, 
    _In_ uint8_t        *Buffer,
    _In_ size_t          Size,
    _In_ Flags_t         Flags);

/* PipeDestroy
 * Destroys a pipe and wakes up all sleeping threads, then
 * frees all resources allocated */
KERNELAPI
void
KERNELABI
PipeDestroy(
    _In_ MCorePipe_t    *Pipe);

/* PipeAcquire
 * Acquires memory space in the pipe. The memory is not
 * visible at this point, stage 1 in the write-process. */
KERNELAPI
OsStatus_t
KERNELABI
PipeAcquire(
    _In_ MCorePipe_t    *Pipe,
    _In_ size_t          Length,
    _Out_ void         **Buffer,
    _Out_ int           *Id);

/* PipeCommit
 * Registers the data available and wakes up consumer. */
KERNELAPI
OsStatus_t
KERNELABI
PipeCommit(
    _In_ MCorePipe_t    *Pipe,
    _In_ uint8_t        *Data,
    _In_ int             Id);

/* PipeConsume
 * Consumes the requested amount of data from the queue. */
KERNELAPI
OsStatus_t
KERNELABI
PipeConsume(
    _In_ MCorePipe_t    *Pipe, 
    _In_ uint8_t        *Buffer,
    _In_ size_t          Length);

/* PipeBytesAvailable
 * Returns how many bytes are available in buffer to be read */
KERNELAPI
int
KERNELABI
PipeBytesAvailable(
    _In_ MCorePipe_t    *Pipe);

/* PipeBytesLeft
 * Returns how many bytes are ready for usage/able to be written */
KERNELAPI
int
KERNELABI
PipeBytesLeft(
    _In_ MCorePipe_t    *Pipe);

#endif // !_MCORE_PIPE_H_

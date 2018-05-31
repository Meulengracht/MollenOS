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
 *  - Builds on principle of a ringbuffer and a multiple-producer
 *    multiple-consumer queue. The queue is completely lock-less but
 *    not wait-less. This is by design.
 */

#ifndef _MCORE_PIPE_H_
#define _MCORE_PIPE_H_

#include <os/osdefs.h>
#include <atomicsection.h>
#include <semaphore.h>

/* Customization of the pipe, these are some default
 * parameters for creation */
#define PIPE_DEFAULT_SIZE           32768
#define PIPE_WORKERS                512
#define PIPE_WORKERS_MASK           PIPE_WORKERS - 1

/* MCorePipe
 * The pipe structure, it basically contains
 * what equals to a ringbuffer, except it adds
 * read/write queues in order to block users */
typedef struct _MCorePipe {
    Flags_t         Flags;
    uint8_t*        Buffer;
    size_t          Length;
    Semaphore_t     WriteQueue;
    int             WritersInQueue;

    atomic_uint     DataWrite;
    atomic_uint     DataRead;
    atomic_uint     WriteWorker;
    atomic_uint     ReadWorker;
    struct {
        AtomicSection_t SyncObject;
        unsigned        IndexData;
        unsigned        LengthData;
        uint8_t         Flags;
    } Workers[PIPE_WORKERS];
} MCorePipe_t;

#define PIPE_WORKER_ALLOCATED   (1 << 0)
#define PIPE_WORKER_REGISTERED  (1 << 1)

/* PipeCreate
 * Initialise a new pipe of the given size and with the given flags */
KERNELAPI MCorePipe_t* KERNELABI
PipeCreate(
    _In_ size_t         Size, 
    _In_ Flags_t        Flags);

/* PipeConstruct
 * Construct an already existing pipe by resetting the
 * pipe with the given parameters */
KERNELAPI void KERNELABI
PipeConstruct(
    _In_ MCorePipe_t*   Pipe, 
    _In_ uint8_t*       Buffer,
    _In_ size_t         Size,
    _In_ Flags_t        Flags);

/* PipeDestroy
 * Destroys a pipe and wakes up all sleeping threads, then
 * frees all resources allocated */
KERNELAPI void KERNELABI
PipeDestroy(
    _In_ MCorePipe_t*   Pipe);

/* PipeProduceAcquire
 * Acquires memory space in the pipe. The memory is not
 * visible at this point, stage 1 in the write-process. */
KERNELAPI OsStatus_t KERNELABI
PipeProduceAcquire(
    _In_  MCorePipe_t*  Pipe,
    _In_  size_t        Length,
    _Out_ unsigned*     Worker,
    _Out_ unsigned*     Index);

/* PipeProduce
 * Produces data for the consumer by adding to allocated worker. */
KERNELAPI size_t KERNELABI
PipeProduce(
    _In_ MCorePipe_t*   Pipe,
    _In_ uint8_t*       Data,
    _In_ size_t         Length,
    _InOut_ unsigned*   Index);

/* PipeProduceCommit
 * Registers the data available and wakes up consumer. */
KERNELAPI OsStatus_t KERNELABI
PipeProduceCommit(
    _In_ MCorePipe_t*   Pipe,
    _In_ unsigned       Worker);

/* PipeConsumeAcquire
 * Acquires the next available worker. */
KERNELAPI OsStatus_t KERNELABI
PipeConsumeAcquire(
    _In_  MCorePipe_t*  Pipe,
    _Out_ unsigned*     Worker);

/* PipeConsume
 * Consumes data available from the given worker. */
KERNELAPI size_t KERNELABI
PipeConsume(
    _In_ MCorePipe_t*   Pipe,
    _In_ uint8_t*       Data,
    _In_ size_t         Length,
    _In_ unsigned       Worker);

/* PipeConsumeCommit
 * Registers the worker as available and wakes up producers. */
KERNELAPI OsStatus_t KERNELABI
PipeConsumeCommit(
    _In_ MCorePipe_t*   Pipe,
    _In_ unsigned       Worker);

#endif // !_MCORE_PIPE_H_

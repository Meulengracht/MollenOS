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
#define __MODULE "PIPE"
//#define __TRACE

/* Includes 
 * - System */
#include <scheduler.h>
#include <debug.h>
#include <pipe.h>
#include <heap.h>

/* Includes
 * - Library */
#include <stddef.h>
#include <string.h>

/* PipeCreate
 * Initialise a new pipe of the given size and with the given flags */
MCorePipe_t*
PipeCreate(
    _In_ size_t         Size, 
    _In_ Flags_t        Flags)
{
    // Allocate both a pipe and a 
    // buffer in kernel memory for the pipe data
    MCorePipe_t *Pipe = (MCorePipe_t*)kmalloc(sizeof(MCorePipe_t));
    Pipe->Buffer = (uint8_t*)kmalloc(Size);
    PipeConstruct(Pipe, Pipe->Buffer, Size, Flags);
    return Pipe;
}

/* PipeConstruct
 * Construct an already existing pipe by resetting the
 * pipe with the given parameters */
void
PipeConstruct(
    _In_ MCorePipe_t    *Pipe, 
    _In_ uint8_t        *Buffer,
    _In_ size_t          Size,
    _In_ Flags_t         Flags)
{
    // Variables
    int i;

    // Update members
    memset((void*)Buffer, 0, sizeof(Size));
    SemaphoreConstruct(&Pipe->WriteQueue, 0, 0);
    Pipe->WritersInQueue    = 0;
    Pipe->Length            = Size;
    Pipe->Buffer            = Buffer;
    Pipe->WriteWorker       = ATOMIC_VAR_INIT(0);
    Pipe->ReadWorker        = ATOMIC_VAR_INIT(0);
    Pipe->DataWrite         = ATOMIC_VAR_INIT(0);
    Pipe->DataRead          = ATOMIC_VAR_INIT(0);
    for (i = 0; i < PIPE_WORKERS; i++) {
        Pipe->Workers[i].IndexData  = 0;
        Pipe->Workers[i].Allocated  = ATOMIC_VAR_INIT(0);
        Pipe->Workers[i].Registered = ATOMIC_VAR_INIT(0);
    }
}

/* PipeDestroy
 * Destroys a pipe and wakes up all sleeping threads, then
 * frees all resources allocated */
void
PipeDestroy(
    _In_ MCorePipe_t    *Pipe)
{
    // Wake all up so no-one is left behind
    SchedulerHandleSignalAll((uintptr_t*)&Pipe->WriteQueue);
    kfree(Pipe->Buffer);
    kfree(Pipe);
}

/* PipeProduceAcquire
 * Acquires memory space in the pipe. The memory is not
 * visible at this point, stage 1 in the write-process. */
OsStatus_t
PipeProduceAcquire(
    _In_ MCorePipe_t    *Pipe,
    _In_ size_t          Length,
    _Out_ unsigned      *Worker,
    _Out_ unsigned      *Index)
{
    // Variables
    unsigned AcquiredWorker = 0;

    // Debug
    TRACE("PipeProduceAcquire(Length %u)", Length);

    // Allocate a place in the pipe
    AcquiredWorker = atomic_fetch_add(&Pipe->WriteWorker, 1);
    AcquiredWorker &= PIPE_WORKERS_MASK;
    while (atomic_exchange(&Pipe->Workers[AcquiredWorker].Allocated, 1) != 0) {
        SchedulerThreadSleep((uintptr_t*)&Pipe->Workers[AcquiredWorker], 0);
    }

    // Acquire space in the buffer
    Pipe->Workers[AcquiredWorker].IndexData = atomic_fetch_add(&Pipe->DataWrite, Length);

    // Update outs
    *Worker = AcquiredWorker;
    *Index = Pipe->Workers[AcquiredWorker].IndexData;
    return OsSuccess;
}

/* PipeProduce
 * Produces data for the consumer by adding to allocated worker. */
size_t
PipeProduce(
    _In_ MCorePipe_t    *Pipe,
    _In_ uint8_t        *Data,
    _In_ size_t          Length,
    _InOut_ unsigned    *Index)
{
    // Variables
    unsigned CurrentReaderIndex     = 0;
    unsigned CurrentWriterIndex     = *Index;
    size_t DataProduced             = 0;
    
    // Debug
    TRACE("PipeProduce(Index %u, Length %u)", 
        CurrentWriterIndex, Length);

    while (DataProduced < Length) {
        CurrentReaderIndex = atomic_load(&Pipe->DataRead);
        while (((CurrentWriterIndex - CurrentReaderIndex) < Pipe->Length)
                && DataProduced < Length) {
            Pipe->Buffer[CurrentWriterIndex++ & (Pipe->Length - 1)] = Data[DataProduced++];
        }
        
        // We must wait for there to be room in pipe
        if (DataProduced != Length) {
            Pipe->WritersInQueue++;
            SemaphoreWait(&Pipe->WriteQueue, 0);
        }
    }
    *Index = CurrentWriterIndex;
    return DataProduced;
}

/* PipeProduceCommit
 * Registers the data available and wakes up consumer. */
OsStatus_t
PipeProduceCommit(
    _In_ MCorePipe_t    *Pipe,
    _In_ unsigned        Worker)
{
    // Debug
    TRACE("PipeProduceCommit(Worker %u)", Worker);

    // Register us and signal
    atomic_store(&Pipe->Workers[Worker].Registered, 1);
    SchedulerHandleSignal((uintptr_t*)&Pipe->Workers[Worker]);
    return OsSuccess;
}

/* PipeConsumeAcquire
 * Acquires the next available worker. */
OsStatus_t
PipeConsumeAcquire(
    _In_ MCorePipe_t    *Pipe,
    _Out_ unsigned      *Worker)
{
    // Variables
    unsigned AcquiredReader = 0;

    // Debug
    TRACE("PipeConsumeAcquire()");

    // Allocate a place in the pipe
    AcquiredReader = atomic_fetch_add(&Pipe->ReadWorker, 1);
    AcquiredReader &= PIPE_WORKERS_MASK;
    while (atomic_load(&Pipe->Workers[AcquiredReader].Registered) != 1) {
        SchedulerThreadSleep((uintptr_t*)&Pipe->Workers[AcquiredReader], 0);
    }
    
    // Update outs
    *Worker = AcquiredReader;
    return OsSuccess;
}

/* PipeConsume
 * Consumes data available from the given worker. */
size_t
PipeConsume(
    _In_ MCorePipe_t    *Pipe,
    _In_ uint8_t        *Data,
    _In_ size_t          Length,
    _In_ unsigned        Worker)
{
    // Variables
    size_t DataConsumed = 0;

    // Debug
    TRACE("PipeConsume(Length %u, Worker %u)", Length, Worker);

    while (DataConsumed < Length) {
        Data[DataConsumed++] = Pipe->Buffer[Pipe->Workers[Worker].IndexData & (Pipe->Length - 1)];
        Pipe->Workers[Worker].IndexData++;
    }
    atomic_fetch_add(&Pipe->DataRead, DataConsumed);
    if (Pipe->WritersInQueue != 0) {
        SemaphoreSignal(&Pipe->WriteQueue, Pipe->WritersInQueue);
        Pipe->WritersInQueue = 0;
    }
    return DataConsumed;
}

/* PipeConsumeCommit
 * Registers the worker as available and wakes up producers. */
OsStatus_t
PipeConsumeCommit(
    _In_ MCorePipe_t    *Pipe,
    _In_ unsigned        Worker)
{
    // Debug
    TRACE("PipeConsumeCommit(Worker %u)", Worker);

    // Register us and signal
    atomic_store(&Pipe->Workers[Worker].Allocated, 0);
    atomic_store(&Pipe->Workers[Worker].Registered, 0);
    SchedulerHandleSignal((uintptr_t*)&Pipe->Workers[Worker]);
    return OsSuccess;
}

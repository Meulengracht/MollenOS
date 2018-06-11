/* MollenOS
 *
 * Copyright 2017, Philip Meulengracht
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
#include <assert.h>

/* PipeCreate
 * Initialise a new pipe of the given size and with the given flags */
MCorePipe_t*
PipeCreate(
    _In_ size_t         Size, 
    _In_ Flags_t        Flags)
{
    // Allocate both a pipe and a 
    // buffer in kernel memory for the pipe data
    MCorePipe_t *Pipe   = (MCorePipe_t*)kmalloc(sizeof(MCorePipe_t));
    Pipe->Buffer        = (uint8_t*)kmalloc(Size);
    PipeConstruct(Pipe, Pipe->Buffer, Size, Flags);
    return Pipe;
}

/* PipeConstruct
 * Construct an already existing pipe by resetting the
 * pipe with the given parameters */
void
PipeConstruct(
    _In_ MCorePipe_t*   Pipe, 
    _In_ uint8_t*       Buffer,
    _In_ size_t         Size,
    _In_ Flags_t        Flags)
{
    // Update members
    memset((void*)Pipe,     0, sizeof(MCorePipe_t));
    memset((void*)Buffer,   0, sizeof(Size));
    SemaphoreConstruct(&Pipe->WriteQueue, 0, 0);
    Pipe->Length    = Size;
    Pipe->Buffer    = Buffer;
}

/* PipeDestroy
 * Destroys a pipe and wakes up all sleeping threads, then
 * frees all resources allocated */
void
PipeDestroy(
    _In_ MCorePipe_t*   Pipe)
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
    _In_  MCorePipe_t*  Pipe,
    _In_  size_t        Length,
    _Out_ unsigned*     Worker,
    _Out_ unsigned*     Index)
{
    // Variables
    unsigned AcquiredWorker = 0;
    uint8_t Flags;

    // Debug
    TRACE("PipeProduceAcquire(Length %u)", Length);

    // Allocate a place in the pipe
    AcquiredWorker  = atomic_fetch_add(&Pipe->WriteWorker, 1);
    AcquiredWorker &= PIPE_WORKERS_MASK;

    Flags           = atomic_load(&Pipe->Workers[AcquiredWorker].Flags);
SyncWithWorker:
    assert((Flags & PIPE_WORKER_PRODUCER_WAITING) == 0); // find solution @todo

    // Is it allocated?
    if (Flags & PIPE_WORKER_ALLOCATED) {
        uint8_t UpdatedFlags = Flags | PIPE_WORKER_PRODUCER_WAITING;
        if (!atomic_compare_exchange_weak(&Pipe->Workers[AcquiredWorker].Flags, 
            &Flags, UpdatedFlags)) {
            goto SyncWithWorker;
        }
        SchedulerThreadSleep((uintptr_t*)&Pipe->Workers[AcquiredWorker], 0);

        // Sync flags and tell we are not waiting anymore
        Flags = atomic_load(&Pipe->Workers[AcquiredWorker].Flags);
SyncFlags:
        assert((Flags & PIPE_WORKER_PRODUCER_WAITING) != 0);

        UpdatedFlags = Flags & ~(PIPE_WORKER_PRODUCER_WAITING);
        UpdatedFlags |= PIPE_WORKER_ALLOCATED;
        if (!atomic_compare_exchange_weak(&Pipe->Workers[AcquiredWorker].Flags,
            &Flags, UpdatedFlags)) {
            goto SyncFlags;
        }
    }
    else {
        uint8_t UpdatedFlags = Flags | PIPE_WORKER_ALLOCATED;
        if (!atomic_compare_exchange_weak(&Pipe->Workers[AcquiredWorker].Flags, 
            &Flags, UpdatedFlags)) {
            goto SyncWithWorker;
        }
    }

    // Acquire space in the buffer
    Pipe->Workers[AcquiredWorker].IndexData     = atomic_fetch_add(&Pipe->DataWrite, Length);
    Pipe->Workers[AcquiredWorker].LengthData    = Length;

    // Update outs
    *Worker = AcquiredWorker;
    *Index  = Pipe->Workers[AcquiredWorker].IndexData;
    return OsSuccess;
}

/* PipeProduce
 * Produces data for the consumer by adding to allocated worker. */
size_t
PipeProduce(
    _In_ MCorePipe_t*   Pipe,
    _In_ uint8_t*       Data,
    _In_ size_t         Length,
    _InOut_ unsigned*   Index)
{
    // Variables
    unsigned CurrentReaderIndex     = 0;
    unsigned CurrentWriterIndex     = *Index;
    size_t DataProduced             = 0;
    
    // Debug
    TRACE("PipeProduce(Index %u, Length %u)", CurrentWriterIndex, Length);

    while (DataProduced < Length) {
        CurrentReaderIndex = atomic_load(&Pipe->DataRead);
        while (((CurrentWriterIndex - CurrentReaderIndex) < Pipe->Length) && DataProduced < Length) {
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
    _In_ MCorePipe_t*   Pipe,
    _In_ unsigned       Worker)
{
    // Variables
    uint8_t Flags, UpdatedFlags;

    // Debug
    TRACE("PipeProduceCommit(Worker %u)", Worker);

    // Register us and signal
    Flags           = atomic_load(&Pipe->Workers[Worker].Flags);
SyncWithWorker:
    UpdatedFlags    = Flags | PIPE_WORKER_REGISTERED;
    if (!atomic_compare_exchange_weak(&Pipe->Workers[Worker].Flags, 
        &Flags, UpdatedFlags)) {
        goto SyncWithWorker;
    }

    // Wake any waiting thread
    if (Flags & PIPE_WORKER_CONSUMER_WAITING) {
        while (SchedulerHandleSignal((uintptr_t*)&Pipe->Workers[Worker]) == OsError);
    }
    WARNING("Produce, Done, %u", Flags);
    return OsSuccess;
}

/* PipeConsumeAcquire
 * Acquires the next available worker. */
OsStatus_t
PipeConsumeAcquire(
    _In_  MCorePipe_t*  Pipe,
    _Out_ unsigned*     Worker)
{
    // Variables
    unsigned AcquiredReader = 0;
    uint8_t Flags;

    // Debug
    TRACE("PipeConsumeAcquire()");

    // Allocate a place in the pipe
    AcquiredReader = atomic_fetch_add(&Pipe->ReadWorker, 1);
    AcquiredReader &= PIPE_WORKERS_MASK;

    Flags           = atomic_load(&Pipe->Workers[AcquiredReader].Flags);
SyncWithWorker:
    assert((Flags & PIPE_WORKER_CONSUMER_WAITING) == 0); // find solution @todo

    // Is the entry registered?
    if (!(Flags & PIPE_WORKER_REGISTERED)) {
        uint8_t UpdatedFlags = Flags | PIPE_WORKER_CONSUMER_WAITING;
        if (!atomic_compare_exchange_weak(&Pipe->Workers[AcquiredReader].Flags, 
            &Flags, UpdatedFlags)) {
            goto SyncWithWorker;
        }
        SchedulerThreadSleep((uintptr_t*)&Pipe->Workers[AcquiredReader], 0);

        // Sync flags and tell we are not waiting anymore
        Flags = atomic_load(&Pipe->Workers[AcquiredReader].Flags);
        WARNING("Consume, Awake, %u", Flags);
SyncFlags:
        assert((Flags & PIPE_WORKER_CONSUMER_WAITING) != 0);

        UpdatedFlags = Flags & ~(PIPE_WORKER_CONSUMER_WAITING);
        if (!atomic_compare_exchange_weak(&Pipe->Workers[AcquiredReader].Flags,
            &Flags, UpdatedFlags)) {
            goto SyncFlags;
        }
    }

    // Update outs
    *Worker = AcquiredReader;
    return OsSuccess;
}

/* PipeConsume
 * Consumes data available from the given worker. */
size_t
PipeConsume(
    _In_ MCorePipe_t*   Pipe,
    _In_ uint8_t*       Data,
    _In_ size_t         Length,
    _In_ unsigned       Worker)
{
    // Variables
    size_t DataConsumed = 0;

    // Debug
    TRACE("PipeConsume(Length %u, Worker %u)", Length, Worker);

    while (DataConsumed < Length && DataConsumed < Pipe->Workers[Worker].LengthData) {
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
    _In_ MCorePipe_t*   Pipe,
    _In_ unsigned       Worker)
{
    // Variables
    uint8_t Flags, UpdatedFlags;

    // Debug
    TRACE("PipeConsumeCommit(Worker %u)", Worker);

    // Register us and signal
    Flags           = atomic_load(&Pipe->Workers[Worker].Flags);
SyncWithWorker:
    UpdatedFlags    = Flags & ~(PIPE_WORKER_ALLOCATED | PIPE_WORKER_REGISTERED);
    if (!atomic_compare_exchange_weak(&Pipe->Workers[Worker].Flags, 
        &Flags, UpdatedFlags)) {
        goto SyncWithWorker;
    }

    // Wake any waiting thread
    if (Flags & PIPE_WORKER_PRODUCER_WAITING) {
        while (SchedulerHandleSignal((uintptr_t*)&Pipe->Workers[Worker]) == OsError);
    }
    return OsSuccess;
}

/* MollenOS
 *
 * Copyright 2018, Philip Meulengracht
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
 * Pipe Interface
 *  - This multi-interface queue is a lock-less thread-safe implementation of pipe-modes:
 *      - Bounded MPMC / MPSC / SPMC
 *      - Unbounded MPMC / MPSC / SPMC
 *      - Bounded SPSC
 */

#ifndef __SYSTEM_PIPE__
#define __SYSTEM_PIPE__

#include <os/osdefs.h>
#include <semaphore.h>

#define PIPE_DEFAULT_ENTRYCOUNT     8 // Logarithmic base of 2 value of workers

// Default configuration if passing 0 is
// Single Producer, Single Consumer, Bounded, Raw
// Unbounded can only be used when the queue is not configured as SPSC
#define PIPE_MULTIPLE_PRODUCERS     (1 << 0)
#define PIPE_MULTIPLE_CONSUMERS     (1 << 1)
#define PIPE_UNBOUNDED              (1 << 2)
#define PIPE_STRUCTURED_BUFFER      (1 << 3)
#define PIPE_NOBLOCK                (1 << 4)

#define PIPE_MPMC                   (PIPE_MULTIPLE_PRODUCERS | PIPE_MULTIPLE_CONSUMERS)

typedef struct _SystemPipeEntry {
    Semaphore_t  SyncObject;
    unsigned int SegmentBufferIndex;
    unsigned int SegmentBufferCurrentIndex;
    uint16_t     Length;
} SystemPipeEntry_t;

typedef struct _SystemPipeSegmentBuffer {
    Mutex_t     LockObject;
    Semaphore_t ReaderSync;
    Semaphore_t WriterSync;
    uint8_t*    Pointer;
    size_t      Size;           // Must be a power of 2.

    atomic_uint ReadPointer;
    atomic_uint ReadCommitted;
    atomic_uint WritePointer;
    atomic_uint WriteCommitted;
} SystemPipeSegmentBuffer_t;

/* SystemPipeSegment
 * A system pipe segment is a collection of entries with a minimum base. */
typedef struct _SystemPipeSegment {
    Mutex_t                             LockObject;
    Semaphore_t                         SyncObject;
    SystemPipeSegmentBuffer_t           Buffer;
    unsigned int                        TicketBase;
    _Atomic(int)                        References;
    SystemPipeEntry_t*                  Entries;
    _Atomic(struct _SystemPipeSegment*) Link;
} SystemPipeSegment_t;

/* SystemPipeProducer
 * A producer for a system pipe, describes the current producer state. */
typedef struct _SystemPipeProducer {
    _Atomic(SystemPipeSegment_t*) Tail;
    atomic_uint                   Ticket;
} SystemPipeProducer_t;

/* SystemPipeConsumer
 * A consumer for a system pipe, describes the current consumer state. */
typedef struct _SystemPipeConsumer {
    _Atomic(SystemPipeSegment_t*) Head;
    atomic_uint                   Ticket;
} SystemPipeConsumer_t;

/* SystemPipeUserState
 * State structure used when reading or writing for queues that support
 * more functionality than SPSC. */
typedef struct _SystemPipeUserState {
    SystemPipeSegment_t* Segment;
    unsigned int         Index;
    int                  Advance;
} SystemPipeUserState_t;

/* SystemPipe
 * A system pipe is the prefered way of communcation between processes.
 * It contains a number of segments, which in turn contains entries that can be used. */
typedef struct _SystemPipe {
    Flags_t              Configuration;
    size_t               Stride;
    size_t               SegmentLgSize;

    SystemPipeConsumer_t ConsumerState;
    SystemPipeProducer_t ProducerState;
} SystemPipe_t;

/* CreateSystemPipe
 * Initialise a new pipe instance with the given configuration and initializes it. */
KERNELAPI SystemPipe_t* KERNELABI
CreateSystemPipe(
    _In_ Flags_t Configuration,
    _In_ size_t  SegmentLgSize);

/* ConstructSystemPipe
 * Construct an already existing pipe by initializing the pipe with the given configuration. */
KERNELAPI void KERNELABI
ConstructSystemPipe(
    _In_ SystemPipe_t*              Pipe,
    _In_ Flags_t                    Configuration,
    _In_ size_t                     SegmentLgSize);

/* DestroySystemPipe
 * Destroys a pipe and wakes up all sleeping threads, then frees all resources allocated */
KERNELAPI OsStatus_t KERNELABI
DestroySystemPipe(
    _In_ void*                      Resource);

/* ReadSystemPipe
 * Performs raw reading that can only be used on pipes opened in SPSC mode. */
KERNELAPI size_t KERNELABI
ReadSystemPipe(
    _In_ SystemPipe_t*              Pipe,
    _In_ uint8_t*                   Data,
    _In_ size_t                     Length);

/* WriteSystemPipe
 * Performs raw writing that can only be used on pipes opened in SPSC mode. */
KERNELAPI size_t KERNELABI
WriteSystemPipe(
    _In_ SystemPipe_t*              Pipe,
    _In_ const uint8_t*             Data,
    _In_ size_t                     Length);

/* AcquireSystemPipeProduction
 * Acquires a new spot in the system pipe for data production. */
KERNELAPI OsStatus_t KERNELABI
AcquireSystemPipeProduction(
    _In_  SystemPipe_t*          Pipe,
    _In_  size_t                 Length,
    _In_  size_t                 Timeout,
    _Out_ SystemPipeUserState_t* State);

/* WriteSystemPipeProduction
 * Writes data into the production spot acquired. This spot is not marked
 * active before the amount of data written is equal to specfied in Acquire. */
KERNELAPI size_t KERNELABI
WriteSystemPipeProduction(
    _In_ SystemPipeUserState_t*     State,
    _In_ const uint8_t*             Data,
    _In_ size_t                     Length);

/* AcquireSystemPipeConsumption
 * Consumes a new production spot in the system pipe. If none are available it will
 * block untill a new entry is available. */
KERNELAPI OsStatus_t KERNELABI
AcquireSystemPipeConsumption(
    _In_  SystemPipe_t*             Pipe,
    _Out_ size_t*                   Length,
    _Out_ SystemPipeUserState_t*    State);

/* ReadSystemPipeConsumption
 * Reads data into the provided buffer from production spot acquired. */
KERNELAPI size_t KERNELABI
ReadSystemPipeConsumption(
    _In_ SystemPipeUserState_t*     State,
    _In_ uint8_t*                   Data,
    _In_ size_t                     Length);

/* FinalizeSystemPipeConsumption
 * Finalizes the consume-process by performing maintience tasks that were assigned
 * for the given entry consumed. */
KERNELAPI void KERNELABI
FinalizeSystemPipeConsumption(
    _In_ SystemPipe_t*              Pipe,
    _In_ SystemPipeUserState_t*     State);

#endif // !__SYSTEM_PIPE__

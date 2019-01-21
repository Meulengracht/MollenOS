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
 * MollenOS Pipe Interface
 *  - This multi-interface queue is a lock-less thread-safe implementation of pipe-modes:
 *      - Bounded MPMC / MPSC / SPMC
 *      - Unbounded MPMC / MPSC / SPMC
 *      - Bounded SPSC
 */
#define __MODULE "PIPE"
//#define __TRACE

#include <system/utils.h>
#include <system/time.h>
#include <scheduler.h>
#include <debug.h>
#include <pipe.h>
#include <heap.h>

#include <stddef.h>
#include <string.h>
#include <assert.h>

// Prototypes
static void CreateSegment(
    _In_ SystemPipe_t*          Pipe,
    _In_ SystemPipeSegment_t**  Segment,
    _In_ unsigned int           TicketBase);

#define TICKETS_PER_SEGMENT(Pipe)   (1 << Pipe->SegmentLgSize)
#define TICKET_INDEX(Pipe, Ticket)  ((Ticket * Pipe->Stride) & (TICKETS_PER_SEGMENT(Pipe) - 1))

/* CreateSystemPipe
 * Initialise a new pipe instance with the given configuration and initializes it. */
SystemPipe_t*
CreateSystemPipe(
    _In_ Flags_t                    Configuration,
    _In_ size_t                     SegmentLgSize)
{
    // Variables
    SystemPipe_t *Pipe;
    Pipe = (SystemPipe_t*)kmalloc(sizeof(SystemPipe_t));
    ConstructSystemPipe(Pipe, Configuration, SegmentLgSize);
    return Pipe;
}

/* ConstructSystemPipe
 * Construct an already existing pipe by initializing the pipe with the given configuration. */
void
ConstructSystemPipe(
    _In_ SystemPipe_t*              Pipe,
    _In_ Flags_t                    Configuration,
    _In_ size_t                     SegmentLgSize)
{
    // Variables
    SystemPipeSegment_t *Segment;

    // Sanitize input
    assert(Pipe != NULL);

    // Initialize pipe instance
    memset((void*)Pipe, 0, sizeof(SystemPipe_t));
    Pipe->Configuration = Configuration;
    Pipe->SegmentLgSize = SegmentLgSize;

    // Stride is the multiplier we apply for the index of the segment
    // this is not used in bounded conditions or SPSC conditions
    Pipe->Stride            = ((Configuration & PIPE_MPMC) != PIPE_MPMC || 
        ((Configuration & PIPE_UNBOUNDED) == 0) || SegmentLgSize <= 1) ? 1 : 27;

    // Initialize first segment
    CreateSegment(Pipe, &Segment, 0);
    atomic_store(&Pipe->ConsumerState.Head, Segment);
    atomic_store(&Pipe->ProducerState.Tail, Segment);
}

/* DestroySystemPipe
 * Destroys a pipe and wakes up all sleeping threads, then frees all resources allocated */
OsStatus_t
DestroySystemPipe(
    _In_ void*                      Resource)
{
    // @todo pipe synchronization with threads waiting
    // for data in pipe.
    kfree(Resource);
    return OsSuccess;
}

/////////////////////////////////////////////////////////////////////////
// System Pipe Helpers Code
static inline SystemPipeSegment_t*
GetSystemPipeHead(
    _In_ SystemPipe_t*              Pipe)
{
    return atomic_load_explicit(&Pipe->ConsumerState.Head, memory_order_acquire);
}

static inline void
SetSystemPipeHead(
    _In_ SystemPipe_t*              Pipe,
    _In_ SystemPipeSegment_t*       Segment)
{
    assert((Pipe->Configuration & PIPE_MULTIPLE_CONSUMERS) == 0);
    atomic_store_explicit(&Pipe->ConsumerState.Head, Segment, memory_order_relaxed);
}

static inline SystemPipeSegment_t*
GetSystemPipeTail(
    _In_ SystemPipe_t*              Pipe)
{
    return atomic_load_explicit(&Pipe->ProducerState.Tail, memory_order_acquire);
}

static inline void
SetSystemPipeTail(
    _In_ SystemPipe_t*              Pipe,
    _In_ SystemPipeSegment_t*       Segment)
{
    assert((Pipe->Configuration & PIPE_MPMC) == 0);
    atomic_store_explicit(&Pipe->ProducerState.Tail, Segment, memory_order_release);
}

static inline _Bool
SwapSystemPipeTail(
    _In_ SystemPipe_t*              Pipe,
    _In_ SystemPipeSegment_t**      Segment,
    _In_ SystemPipeSegment_t*       Next)
{
    assert((Pipe->Configuration & PIPE_MPMC) != 0);
    return atomic_compare_exchange_strong_explicit(&Pipe->ProducerState.Tail, 
        Segment, Next, memory_order_release, memory_order_relaxed);
}

static inline _Bool
SwapSystemPipeHead(
    _In_ SystemPipe_t*              Pipe,
    _In_ SystemPipeSegment_t**      Segment,
    _In_ SystemPipeSegment_t*       Next)
{
    assert(Pipe->Configuration & PIPE_MULTIPLE_CONSUMERS);
    return atomic_compare_exchange_strong_explicit(&Pipe->ConsumerState.Head, 
        Segment, Next, memory_order_release, memory_order_acquire);
}

static inline unsigned int
GetSystemPipeProducerTicket(
    _In_ SystemPipe_t*              Pipe)
{
    if (Pipe->Configuration & PIPE_MULTIPLE_PRODUCERS) {
        return atomic_fetch_add(&Pipe->ProducerState.Ticket, 1);
    }
    else {
        unsigned int Ticket = atomic_load(&Pipe->ProducerState.Ticket);
        atomic_store(&Pipe->ProducerState.Ticket, Ticket + 1);
        return Ticket;
    }
}

static inline unsigned int
GetSystemPipeConsumerTicket(
    _In_ SystemPipe_t*              Pipe)
{
    if (Pipe->Configuration & PIPE_MULTIPLE_PRODUCERS) {
        return atomic_fetch_add(&Pipe->ConsumerState.Ticket, 1);
    }
    else {
        unsigned int Ticket = atomic_load(&Pipe->ConsumerState.Ticket);
        atomic_store(&Pipe->ConsumerState.Ticket, Ticket + 1);
        return Ticket;
    }
}

/////////////////////////////////////////////////////////////////////////
// System Pipe Buffer Code
static inline size_t
CalculateBytesAvailableForWriting(
    _In_ SystemPipeSegmentBuffer_t* Buffer,
    _In_ size_t                     ReadIndex,
    _In_ size_t                     WriteIndex)
{
    // Handle wrap-around
    if (ReadIndex > WriteIndex) {
        if (ReadIndex >= (UINT_MAX - Buffer->Size)) {
            return (ReadIndex & (Buffer->Size - 1)) - (WriteIndex & (Buffer->Size - 1));
        }
        else {
            return 0; // Overcommitted
        }
    }
    return Buffer->Size - (WriteIndex - ReadIndex);
}

static inline size_t
CalculateBytesAvailableForReading(
    _In_ SystemPipeSegmentBuffer_t* Buffer,
    _In_ size_t                     ReadIndex,
    _In_ size_t                     WriteIndex)
{
    // Handle wrap-around
    if (ReadIndex > WriteIndex) {
        if (ReadIndex >= (UINT_MAX - Buffer->Size)) {
            return (Buffer->Size - (ReadIndex & (Buffer->Size - 1))) + 
                (WriteIndex & (Buffer->Size - 1)) - 1;
        }
        else {
            return 0; // Overcommitted
        }
    }
    return WriteIndex - ReadIndex;
}

static void
InitializeSegmentBuffer(
    _In_ SystemPipe_t*              Pipe,
    _In_ SystemPipeSegmentBuffer_t* Buffer)
{
    // If we use a raw queue then we can simplify and significantly speed up
    // the usage of the segment buffer. We use twice the lg size of entries. For 128
    // entries we thus have 32kb buffer, 256 entries we have 64kb.
    Buffer->Size        = (1 << (Pipe->SegmentLgSize * 2));
    Buffer->Pointer     = (uint8_t*)kmalloc(Buffer->Size);
}

static void
DestroySegmentBuffer(
    _In_ SystemPipeSegmentBuffer_t* Buffer)
{
    kfree(Buffer->Pointer);
}

/////////////////////////////////////////////////////////////////////////
// System Pipe Structured Buffer Code
static void
GetSegmentProductionSpot(
    _In_ SystemPipe_t*              Pipe,
    _In_ SystemPipeSegment_t*       Segment)
{
    // Variables
    int ProductionSpots;

    atomic_fetch_add(&Segment->References, 1);
    while (1) {
        ProductionSpots = atomic_load(&Segment->ProductionSpots);
        if (!ProductionSpots) {
            SchedulerAtomicThreadSleep(&Segment->ProductionSpots, &ProductionSpots, 0);
            continue; // Start over
        }

        // Synchronize with other producers
        if (Pipe->Configuration & PIPE_MULTIPLE_PRODUCERS) {
            while (ProductionSpots) {
                if (atomic_compare_exchange_weak(&Segment->ProductionSpots, 
                    &ProductionSpots, ProductionSpots - 1)) {
                    break;
                }
            }

            // Did we end up overcomitting?
            if (!ProductionSpots) {
                continue; // Start write loop all over
            }
            break;
        }
        else {
            // No sweat
            atomic_store_explicit(&Segment->ProductionSpots, ProductionSpots - 1, memory_order_relaxed);
            break;
        }
    }
}

static unsigned int
AcquireSegmentBufferSpace(
    _In_ SystemPipe_t*              Pipe,
    _In_ SystemPipeSegmentBuffer_t* Buffer,
    _In_ size_t                     Length)
{
    // Variables
    unsigned int ReadIndex;
    unsigned int WriteIndex;
    size_t BytesAvailable;

    // Make sure we write all the bytes
    while (1) {
        WriteIndex      = atomic_load(&Buffer->WritePointer);
        ReadIndex       = atomic_load(&Buffer->ReadCommitted);
        BytesAvailable  = MIN(
            CalculateBytesAvailableForWriting(Buffer, ReadIndex, WriteIndex), Length);
        if (BytesAvailable != Length) {
            SchedulerAtomicThreadSleep((atomic_int*)&Buffer->ReadCommitted, (int*)&ReadIndex, 0);
            continue; // Start over
        }

        // Synchronize with other producers
        if (Pipe->Configuration & PIPE_MULTIPLE_PRODUCERS) {
            while (BytesAvailable == Length) {
                size_t NewWritePointer  = WriteIndex + BytesAvailable;
                if (atomic_compare_exchange_weak(&Buffer->WritePointer, &WriteIndex, NewWritePointer)) {
                    break;
                }
                ReadIndex       = atomic_load(&Buffer->ReadCommitted);
                BytesAvailable  = MIN(
                    CalculateBytesAvailableForWriting(Buffer, ReadIndex, WriteIndex), Length);
            }

            // Did we end up overcomitting?
            if (BytesAvailable != Length) {
                continue; // Start write loop all over
            }
        }
        else {
            atomic_store_explicit(&Buffer->WritePointer, WriteIndex + BytesAvailable, memory_order_relaxed);
        }

        // Break us out here
        if (BytesAvailable == Length) {
            break;
        }
    }
    return WriteIndex;
}

static void
WriteSegmentBufferSpace(
    _In_ SystemPipeSegmentBuffer_t* Buffer,
    _In_ const uint8_t*             Data,
    _In_ size_t                     Length,
    _InOut_ unsigned int*           Index)
{
    // Variables
    unsigned int CurrentIndex   = *Index;
    size_t BytesWritten         = 0;
    while (BytesWritten < Length) {
        Buffer->Pointer[(CurrentIndex++ & (Buffer->Size - 1))] = Data[BytesWritten++];
    }
    *Index = CurrentIndex;
}

static void
ReadSegmentBufferSpace(
    _In_ SystemPipeSegmentBuffer_t* Buffer,
    _In_ uint8_t*                   Data,
    _In_ size_t                     Length,
    _InOut_ unsigned int*           Index)
{
    // Variables
    unsigned int CurrentIndex   = *Index;
    size_t BytesRead            = 0;
    while (BytesRead < Length) {
        Data[BytesRead++] = Buffer->Pointer[(CurrentIndex++ & (Buffer->Size - 1))];
    }
    *Index = CurrentIndex;
}

/////////////////////////////////////////////////////////////////////////
// System Pipe Raw Buffer Code
static size_t
WriteRawSegmentBuffer(
    _In_ SystemPipe_t*              Pipe,
    _In_ SystemPipeSegmentBuffer_t* Buffer,
    _In_ const uint8_t*             Data,
    _In_ size_t                     Length)
{
    // Variables
    unsigned int ReadIndex;
    unsigned int WriteIndex;
    size_t BytesWritten = 0;
    size_t BytesAvailable;
    size_t BytesCommitted;

    // Make sure we write all the bytes
    while (BytesWritten < Length) {
        WriteIndex      = atomic_load(&Buffer->WritePointer);
        ReadIndex       = atomic_load(&Buffer->ReadCommitted);
        BytesAvailable  = MIN(
            CalculateBytesAvailableForWriting(Buffer, ReadIndex, WriteIndex),
            Length - BytesWritten);
        BytesCommitted  = BytesAvailable;
        if (!BytesAvailable) {
            if (Pipe->Configuration & PIPE_NOBLOCK) {
                break;
            }
            SchedulerAtomicThreadSleep((atomic_int*)&Buffer->ReadCommitted, (int*)&ReadIndex, 0);
            continue; // Start over
        }

        // Synchronize with other producers
        if (Pipe->Configuration & PIPE_MULTIPLE_PRODUCERS) {
            while (BytesAvailable) {
                size_t NewWritePointer  = WriteIndex + BytesAvailable;
                if (atomic_compare_exchange_weak(&Buffer->WritePointer, &WriteIndex, NewWritePointer)) {
                    break;
                }
                ReadIndex       = atomic_load(&Buffer->ReadCommitted);
                BytesAvailable  = MIN(
                    CalculateBytesAvailableForWriting(Buffer, ReadIndex, WriteIndex),
                    Length - BytesWritten);
            }

            if (!BytesAvailable) {
                continue; // Start over as we ran out
            }

            // Wait for our turn
            BytesCommitted = atomic_load(&Buffer->WriteCommitted);
            while (BytesCommitted < WriteIndex) {
                BytesCommitted = atomic_load(&Buffer->WriteCommitted);
            }
            BytesCommitted = BytesAvailable;
        }
        else {
            atomic_store_explicit(&Buffer->WritePointer, WriteIndex + BytesAvailable, memory_order_relaxed);
        }

        // Write the data to the internal buffer
        while (BytesAvailable--) {
            Buffer->Pointer[(WriteIndex++ & (Buffer->Size - 1))] = Data[BytesWritten++];
        }
        atomic_fetch_add(&Buffer->WriteCommitted, BytesCommitted);
        SchedulerHandleSignal((uintptr_t*)&Buffer->WriteCommitted);
    }
    return BytesWritten;
}

static size_t
ReadRawSegmentBuffer(
    _In_ SystemPipe_t*              Pipe,
    _In_ SystemPipeSegmentBuffer_t* Buffer,
    _In_ uint8_t*                   Data,
    _In_ size_t                     Length)
{
    // Variables
    unsigned int ReadIndex;
    unsigned int WriteIndex;
    size_t BytesAvailable   = 0;
    size_t BytesRead        = 0;
    size_t BytesCommitted;
    
    // Make sure there are bytes to read
    while (BytesRead < Length) {
        // Use the write-comitted
        WriteIndex      = atomic_load(&Buffer->WriteCommitted);
        ReadIndex       = atomic_load(&Buffer->ReadPointer);
        BytesAvailable  = MIN(
            CalculateBytesAvailableForReading(Buffer, ReadIndex, WriteIndex), 
            Length - BytesRead);
        BytesCommitted  = BytesAvailable;
        if (!BytesAvailable) {
            if (Pipe->Configuration & PIPE_NOBLOCK) {
                break;
            }
            SchedulerAtomicThreadSleep((atomic_int*)&Buffer->WriteCommitted, (int*)&WriteIndex, 0);
            continue; // Start over
        }

        // Synchronize with other consumers
        if (Pipe->Configuration & PIPE_MULTIPLE_CONSUMERS) {
            while (BytesAvailable) {
                size_t NewReadPointer   = ReadIndex + BytesAvailable;
                if (atomic_compare_exchange_weak(&Buffer->ReadPointer, &ReadIndex, NewReadPointer)) {
                    break;
                }
                WriteIndex      = atomic_load(&Buffer->WriteCommitted);
                BytesAvailable  = MIN(
                    CalculateBytesAvailableForReading(Buffer, ReadIndex, WriteIndex), 
                    Length - BytesRead);
            }

            if (!BytesAvailable) {
                continue; // Start over as we ran out
            }

            // Wait for our turn
            BytesCommitted = atomic_load(&Buffer->ReadCommitted);
            while (BytesCommitted < ReadIndex) {
                BytesCommitted = atomic_load(&Buffer->ReadCommitted);
            }
            BytesCommitted = BytesAvailable;
        }
        else {
            atomic_store_explicit(&Buffer->ReadPointer, ReadIndex + BytesAvailable, memory_order_relaxed);
        }

        // Write the data to the provided buffer
        while (BytesAvailable--) {
            Data[BytesRead++] = Buffer->Pointer[(ReadIndex++ & (Buffer->Size - 1))];
        }
        atomic_fetch_add(&Buffer->ReadCommitted, BytesCommitted);
        SchedulerHandleSignal((uintptr_t*)&Buffer->ReadCommitted);

        // If it was possible read bytes, return. With raw bytes we allow
        // the reader to read less, however never allow to read 0
        if (BytesRead > 0) {
            break;
        }
    }
    return BytesRead;
}

/////////////////////////////////////////////////////////////////////////
// System Pipe Entry Code
static void
InitializeSegmentEntry(
    _In_ SystemPipeEntry_t*         Entry)
{
    SlimSemaphoreConstruct(&Entry->SyncObject, 0, 1);
}

static SystemPipeEntry_t*
GetSegmentEntryForReading(
    _In_ SystemPipeSegment_t*       Segment,
    _In_ unsigned int               Index)
{
    SlimSemaphoreWait(&Segment->Entries[Index].SyncObject, 0);
    return &Segment->Entries[Index];
}

static void
SetSegmentEntryWriteable(
    _In_ SystemPipe_t*              Pipe,
    _In_ SystemPipeSegment_t*       Segment,
    _In_ SystemPipeEntry_t*         Entry)
{
    // Mark buffer read and free, and wakeup writers
    atomic_fetch_add(&Segment->Buffer.ReadCommitted, Entry->Length);
    SchedulerHandleSignal((uintptr_t*)&Segment->Buffer.ReadCommitted);

    // No need to signal if we are unbounded, we don't reuse spots
    if (!(Pipe->Configuration & PIPE_UNBOUNDED)) {
        Entry->Length = 0;
        atomic_fetch_add(&Segment->ProductionSpots, 1);
        SchedulerHandleSignal((uintptr_t*)&Segment->ProductionSpots);
    }
    atomic_fetch_sub(&Segment->References, 1);
}

static SystemPipeEntry_t*
GetSegmentEntryForWriting(
    _In_ SystemPipe_t*              Pipe,
    _In_ SystemPipeSegment_t*       Segment,
    _In_ unsigned int               Index,
    _In_ size_t                     Length)
{
    // Variables
    unsigned int AcquiredIndex;

    // Gain access to the entry, and then gain buffer space
    AcquiredIndex = AcquireSegmentBufferSpace(Pipe, &Segment->Buffer, Length);

    // Setup rest of entry
    Segment->Entries[Index].SegmentBufferIndex          = AcquiredIndex;
    Segment->Entries[Index].SegmentBufferCurrentIndex   = AcquiredIndex;
    Segment->Entries[Index].Length                      = Length;
    return &Segment->Entries[Index];
}

static void
SetSegmentEntryReadable(
    _In_ SystemPipeEntry_t*         Entry)
{
    // Reset current index so it's available for the reader to use
    // and change state of entry to <Unwriteable> <Readable>
    Entry->SegmentBufferCurrentIndex = Entry->SegmentBufferIndex;
    SlimSemaphoreSignal(&Entry->SyncObject, 1);
}

/////////////////////////////////////////////////////////////////////////
// System Pipe Segment Code
static void
CreateSegment(
    _In_ SystemPipe_t*              Pipe,
    _In_ SystemPipeSegment_t**      Segment,
    _In_ unsigned int               TicketBase)
{
    // Variables
    SystemPipeSegment_t* Pointer;
    size_t BytesToAllocate = sizeof(SystemPipeSegment_t);
    int i;

    // Perform the allocation
    if (Pipe->Configuration & PIPE_STRUCTURED_BUFFER) {
        BytesToAllocate += (sizeof(SystemPipeEntry_t) * TICKETS_PER_SEGMENT(Pipe));
    }
    Pointer         = (SystemPipeSegment_t*)kmalloc(BytesToAllocate);
    assert(Pointer != NULL);
    memset((void*)Pointer, 0, BytesToAllocate);

    InitializeSegmentBuffer(Pipe, &Pointer->Buffer);
    Pointer->TicketBase     = TicketBase;
    if (Pipe->Configuration & PIPE_STRUCTURED_BUFFER) {
        Pointer->ProductionSpots    = ATOMIC_VAR_INIT(TICKETS_PER_SEGMENT(Pipe));
        Pointer->Entries            = (SystemPipeEntry_t*)((uint8_t*)Pointer + sizeof(SystemPipeSegment_t));
        for (i = 0; i < TICKETS_PER_SEGMENT(Pipe); i++) {
            InitializeSegmentEntry(&Pointer->Entries[i]);
        }
    }
    *Segment = Pointer;
}

static void
DestroySegment(
    _In_ SystemPipeSegment_t*       Segment)
{
    DestroySegmentBuffer(&Segment->Buffer);
    kfree(Segment);
}

static SystemPipeSegment_t*
GetNextSegment(
    _In_ SystemPipeSegment_t*       Segment)
{
    return atomic_load_explicit(&Segment->Link, memory_order_acquire);
}

static _Bool
SetNextSegment(
    _In_ SystemPipeSegment_t*       Segment,
    _In_ SystemPipeSegment_t*       Next)
{
    SystemPipeSegment_t* Expected = NULL;
    return atomic_compare_exchange_strong_explicit(&Segment->Link, &Expected, Next,
        memory_order_release, memory_order_relaxed);
}

static void
ReclaimSegment(
    _In_ SystemPipeSegment_t*       Segment)
{
    int References = atomic_fetch_sub(&Segment->References, 1) - 1;
    if (References == 0) {
        DestroySegment(Segment);
    }
}

static SystemPipeSegment_t*
CreateNextSystemPipeSegment(
    _In_ SystemPipe_t*              Pipe,
    _In_ SystemPipeSegment_t*       Segment)
{
    SystemPipeSegment_t *Next;
    CreateSegment(Pipe, &Next, Segment->TicketBase + TICKETS_PER_SEGMENT(Pipe));
    if (!SetNextSegment(Segment, Next)) {
        kfree(Next);
        Next = GetNextSegment(Segment);
    }
    return Next;
}

static SystemPipeSegment_t*
GetNextSystemPipeSegment(
    _In_ SystemPipe_t*              Pipe,
    _In_ SystemPipeSegment_t*       Segment,
    _In_ unsigned int               Ticket)
{
    SystemPipeSegment_t *Next = GetNextSegment(Segment);
    if (Next == NULL) {
        // Prepare everything for a new segment, then use 
        // SetNextSegment to try to update it
        assert(Ticket >= Segment->TicketBase + TICKETS_PER_SEGMENT(Pipe));
        // @todo sleep_diff(Ticket - Segment->TicketBase + TICKETS_PER_SEGMENT);
        Next = CreateNextSystemPipeSegment(Pipe, Segment);
    }
    return Next;
}

static SystemPipeSegment_t*
FindSystemPipeSegment(
    _In_ SystemPipe_t*              Pipe,
    _In_ SystemPipeSegment_t*       Segment,
    _In_ unsigned int               Ticket)
{
    while (Ticket >= (Segment->TicketBase + TICKETS_PER_SEGMENT(Pipe))) {
        Segment = GetNextSystemPipeSegment(Pipe, Segment, Ticket);
        assert(Segment != NULL);
    }
    return Segment;
}

/////////////////////////////////////////////////////////////////////////
// System Pipe Implementation

/* ReadSystemPipe
 * Performs generic reading from a system pipe, and will handle both
 * RAW and STRUCTURED pipes. */
size_t
ReadSystemPipe(
    _In_ SystemPipe_t*              Pipe,
    _In_ uint8_t*                   Data,
    _In_ size_t                     Length)
{
    // Variables
    SystemPipeUserState_t State;
    SystemPipeSegment_t *Segment;
    assert(Pipe != NULL);
    assert(Data != NULL);
    assert(Length > 0);

    // Get head for consumption
    Segment = GetSystemPipeHead(Pipe);
    
    // Handle raw/structured differently
    if (!(Pipe->Configuration & PIPE_STRUCTURED_BUFFER)) {
        Length = ReadRawSegmentBuffer(Pipe, &Segment->Buffer, Data, Length);
    }
    else {
        AcquireSystemPipeConsumption(Pipe, &Length, &State);
        ReadSystemPipeConsumption(&State, Data, Length);
        FinalizeSystemPipeConsumption(Pipe, &State);
    }
    return Length;
}

/* WriteSystemPipe
 * Performs generic simple writing to a system pipe, and will handle both
 * RAW and STRUCTURED pipes. */
size_t
WriteSystemPipe(
    _In_ SystemPipe_t*              Pipe,
    _In_ const uint8_t*             Data,
    _In_ size_t                     Length)
{
    // Variables
    SystemPipeUserState_t State;
    SystemPipeSegment_t *Segment;
    assert(Pipe != NULL);
    assert(Data != NULL);
    assert(Length > 0);

    // Get tail for production
    Segment = GetSystemPipeTail(Pipe);

    // Handle raw/structured differently
    if (!(Pipe->Configuration & PIPE_STRUCTURED_BUFFER)) {
        Length = WriteRawSegmentBuffer(Pipe, &Segment->Buffer, Data, Length);
    }
    else {
        AcquireSystemPipeProduction(Pipe, Length, &State);
        WriteSystemPipeProduction(&State, Data, Length);
    }
    return Length;
}

/////////////////////////////////////////////////////////////////////////
// System Pipe Producer Helper Implementations
static void
AdvanceSystemPipeProductionToTail(
    _In_ SystemPipe_t*              Pipe,
    _In_ unsigned int               Ticket)
{
    SystemPipeSegment_t *Tail = GetSystemPipeTail(Pipe);
    while (Tail->TicketBase < Ticket) {
        SystemPipeSegment_t *Next = GetNextSegment(Tail);
        if (Next == NULL) {
            Next = CreateNextSystemPipeSegment(Pipe, Tail);
        }
        assert(Next != NULL);
        SwapSystemPipeTail(Pipe, &Tail, Next);
        Tail = GetSystemPipeTail(Pipe);
    }
}

static void 
AdvanceSystemPipeProducer(
    _In_ SystemPipe_t*              Pipe,
    _In_ SystemPipeSegment_t*       Segment)
{
    if ((Pipe->Configuration & PIPE_MPMC) == 0) {
        SystemPipeSegment_t *Next = GetNextSegment(Segment);
        assert(Next != NULL);
        SetSystemPipeTail(Pipe, Next);
    }
    else {
        unsigned int Ticket = Segment->TicketBase + TICKETS_PER_SEGMENT(Pipe);
        AdvanceSystemPipeProductionToTail(Pipe, Ticket);
    }
}

/////////////////////////////////////////////////////////////////////////
// System Pipe Produce Implementations

/* AcquireSystemPipeProduction
 * Acquires a new spot in the system pipe for data production. */
OsStatus_t
AcquireSystemPipeProduction(
    _In_  SystemPipe_t*             Pipe,
    _In_  size_t                    Length,
    _Out_ SystemPipeUserState_t*    State)
{
    // Variables
    SystemPipeSegment_t *Segment;
    SystemPipeEntry_t *Entry;
    unsigned int Ticket;
    assert(Pipe != NULL);
    assert(Length > 0 && Length < UINT16_MAX);
    assert(Pipe->Configuration & PIPE_STRUCTURED_BUFFER);

    // Get tail for production
    Segment = GetSystemPipeTail(Pipe);

    if (Pipe->Configuration & PIPE_UNBOUNDED) {
        Ticket = GetSystemPipeProducerTicket(Pipe);
        if (Pipe->Configuration & PIPE_MULTIPLE_PRODUCERS) {
            Segment = FindSystemPipeSegment(Pipe, Segment, Ticket);
        }
        Entry = GetSegmentEntryForWriting(Pipe, Segment, TICKET_INDEX(Pipe, Ticket), Length);

        // Perform post-operations, they include making sure
        // we perform our maintience duties, like securing new segments
        // if we are the last ticket or advancing the consumer
        if ((Ticket & (TICKETS_PER_SEGMENT(Pipe) - 1)) == 0) {
            CreateNextSystemPipeSegment(Pipe, Segment);
        }
        if ((Ticket & (TICKETS_PER_SEGMENT(Pipe) - 1)) == (TICKETS_PER_SEGMENT(Pipe) - 1)) {
            AdvanceSystemPipeProducer(Pipe, Segment);
        }
    }
    else {
        // Wait for a spot in production before acquiring a ticket
        GetSegmentProductionSpot(Pipe, Segment);
        Ticket  = GetSystemPipeProducerTicket(Pipe);
        Entry   = GetSegmentEntryForWriting(Pipe, Segment, TICKET_INDEX(Pipe, Ticket), Length);
    }

    // Update state
    if (State != NULL) {
        State->Advance  = 0;
        State->Index    = TICKET_INDEX(Pipe, Ticket);
        State->Segment  = Segment;
    }
    return OsSuccess;
}

/* WriteSystemPipeProduction
 * Writes data into the production spot acquired. This spot is not marked
 * active before the amount of data written is equal to specfied in Acquire. */
size_t
WriteSystemPipeProduction(
    _In_ SystemPipeUserState_t*     State,
    _In_ const uint8_t*             Data,
    _In_ size_t                     Length)
{
    // Variables
    SystemPipeEntry_t *Entry = &State->Segment->Entries[State->Index];
    size_t BytesAvailable;
    size_t BytesWritten;
    assert(Data != NULL);

    // Calculate correct number of bytes available
    if (Entry->SegmentBufferCurrentIndex < Entry->SegmentBufferIndex) {
        BytesWritten = (UINT_MAX - Entry->SegmentBufferIndex) + Entry->SegmentBufferCurrentIndex;
    }
    else {
        BytesWritten = Entry->SegmentBufferCurrentIndex - Entry->SegmentBufferIndex;
    }
    BytesAvailable = MIN(Length, Entry->Length - BytesWritten);

    if (BytesAvailable > 0) {
        WriteSegmentBufferSpace(&State->Segment->Buffer, Data, BytesAvailable, 
            &Entry->SegmentBufferCurrentIndex);
        if (Entry->Length == (Entry->SegmentBufferCurrentIndex - Entry->SegmentBufferIndex)) {
            SetSegmentEntryReadable(Entry);
        }
    }
    return BytesAvailable;
}

/////////////////////////////////////////////////////////////////////////
// System Pipe Consumer Helper Implementations
static void
AdvanceSystemPipeConsumptionToHead(
    _In_ SystemPipe_t*              Pipe,
    _In_ unsigned int               Ticket)
{
    SystemPipeSegment_t *Head;

    // Tail must not lack behind
    AdvanceSystemPipeProductionToTail(Pipe, Ticket);
    Head = GetSystemPipeHead(Pipe);
    if (!(Pipe->Configuration & PIPE_MULTIPLE_CONSUMERS)) {
        SystemPipeSegment_t *Next = GetNextSegment(Head);
        assert(Ticket == Head->TicketBase + TICKETS_PER_SEGMENT(Pipe));
        assert(Next != NULL);
        SetSystemPipeHead(Pipe, Next);
        ReclaimSegment(Head);
    }
    else {
        while (Head->TicketBase < Ticket) {
            SystemPipeSegment_t *Next = GetNextSegment(Head);
            assert(Next != NULL);
            if (SwapSystemPipeHead(Pipe, &Head, Next)) {
                ReclaimSegment(Head);
                Head = Next;
            }
        }
    }
}

static void 
AdvanceSystemPipeConsumer(
    _In_ SystemPipe_t*              Pipe,
    _In_ SystemPipeSegment_t*       Segment)
{
    if ((Pipe->Configuration & PIPE_MPMC) == 0) {
        SystemPipeSegment_t *Next;
        while (GetSystemPipeTail(Pipe) == Segment) {
            ArchStallProcessorCore(1);
        }
        Next = GetNextSegment(Segment);
        assert(Next != NULL);
        SetSystemPipeHead(Pipe, Next);
        ReclaimSegment(Segment);
    }
    else {
        unsigned int Ticket = Segment->TicketBase + TICKETS_PER_SEGMENT(Pipe);
        AdvanceSystemPipeConsumptionToHead(Pipe, Ticket);
    }
}

/////////////////////////////////////////////////////////////////////////
// System Pipe Consume Implementations

/* AcquireSystemPipeConsumption
 * Consumes a new production spot in the system pipe. If none are available it will
 * block untill a new entry is available. */
OsStatus_t
AcquireSystemPipeConsumption(
    _In_  SystemPipe_t*             Pipe,
    _Out_ size_t*                   Length,
    _Out_ SystemPipeUserState_t*    State)
{
    SystemPipeSegment_t *Segment;
    SystemPipeEntry_t *Entry;
    unsigned int Ticket;
    assert(Pipe != NULL);
    assert(State != NULL);
    assert(Pipe->Configuration & PIPE_STRUCTURED_BUFFER);

    // Get head for consumption
    Segment         = GetSystemPipeHead(Pipe);
    Ticket          = GetSystemPipeConsumerTicket(Pipe);
    State->Advance  = 0;
    
    if (Pipe->Configuration & PIPE_UNBOUNDED) {
        if (Pipe->Configuration & PIPE_MULTIPLE_CONSUMERS) {
            Segment = FindSystemPipeSegment(Pipe, Segment, Ticket);
        }
        Entry = GetSegmentEntryForReading(Segment, TICKET_INDEX(Pipe, Ticket));

        // Perform post-operations, they include making sure
        // we perform our maintience duties, like advancing the consumer
        if ((Ticket & (TICKETS_PER_SEGMENT(Pipe) - 1)) == (TICKETS_PER_SEGMENT(Pipe) - 1)) {
            State->Advance = 1;
        }
    }
    else {
        Entry = GetSegmentEntryForReading(Segment, TICKET_INDEX(Pipe, Ticket));
    }

    State->Segment  = Segment;
    State->Index    = TICKET_INDEX(Pipe, Ticket);
    *Length         = Entry->Length;
    return OsSuccess;
}

/* ReadSystemPipeConsumption
 * Reads data into the provided buffer from production spot acquired. */
size_t
ReadSystemPipeConsumption(
    _In_ SystemPipeUserState_t*     State,
    _In_ uint8_t*                   Data,
    _In_ size_t                     Length)
{
    // Variables
    SystemPipeEntry_t *Entry = &State->Segment->Entries[State->Index];
    size_t BytesAvailable;
    size_t BytesRead;
    assert(Data != NULL);

    // Calculate correct number of bytes available
    if (Entry->SegmentBufferCurrentIndex < Entry->SegmentBufferIndex) {
        BytesRead = (UINT_MAX - Entry->SegmentBufferIndex) + Entry->SegmentBufferCurrentIndex;
    }
    else {
        BytesRead = Entry->SegmentBufferCurrentIndex - Entry->SegmentBufferIndex;
    }
    BytesAvailable = MIN(Length, Entry->Length - BytesRead);

    if (BytesAvailable > 0) {
        ReadSegmentBufferSpace(&State->Segment->Buffer, Data, BytesAvailable, 
            &Entry->SegmentBufferCurrentIndex);
    }
    return BytesAvailable;
}

/* FinalizeSystemPipeConsumption
 * Finalizes the consume-process by performing maintience tasks that were assigned
 * for the given entry consumed. */
void
FinalizeSystemPipeConsumption(
    _In_ SystemPipe_t*              Pipe,
    _In_ SystemPipeUserState_t*     State)
{
    assert(Pipe != NULL);
    assert(State != NULL);

    SetSegmentEntryWriteable(Pipe, State->Segment, &State->Segment->Entries[State->Index]);
    if (Pipe->Configuration & PIPE_UNBOUNDED) {
        if (State->Advance) {
            AdvanceSystemPipeConsumer(Pipe, State->Segment);
        }
    }
}

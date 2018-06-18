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
 *  - This is a ported version of the unbounded queue specified in the folly
 *    repository of the facebook code. I wrote a C version based on C++ algorithm
 *    used in the mentioned project. Supported pipe-modes implemented are:
 *      - Bounded MPMC / MPSC / SPMC
 *      - Unbounded MPMC / MPSC / SPMC
 *      - Bounded SPSC
 */
#define __MODULE "PIPE"
//#define __TRACE

#include <system/utils.h>
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
    _In_ Flags_t                Configuration,
    _In_ size_t                 SegmentLgSize)
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
    _In_ SystemPipe_t*          Pipe,
    _In_ Flags_t                Configuration,
    _In_ size_t                 SegmentLgSize)
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
    Pipe->Stride            = ((Configuration & PIPE_MPMC) == 0 || 
        ((Configuration & PIPE_UNBOUNDED) == 0)|| SegmentLgSize <= 1) ? 1 : 27;
    Pipe->TransferLimit     = ((Configuration & PIPE_MPMC) == 0 || 
        ((Configuration & PIPE_UNBOUNDED) == 0)|| SegmentLgSize <= 1) ? 1 : (TICKETS_PER_SEGMENT(Pipe) / 10);
    SlimSemaphoreConstruct(&Pipe->ProductionQueue, 0, 0);
    atomic_store(&Pipe->Credit, TICKETS_PER_SEGMENT(Pipe));

    // Initialize first segment
    CreateSegment(Pipe, &Segment, 0);
    atomic_store(&Pipe->ConsumerState.Head, Segment);
    atomic_store(&Pipe->ProducerState.Tail, Segment);
}

/* DestroySystemPipe
 * Destroys a pipe and wakes up all sleeping threads, then frees all resources allocated */
void
DestroySystemPipe(
    _In_ SystemPipe_t*      Pipe)
{
    // @todo pipe synchronization with threads waiting
    // for data in pipe.
    kfree(Pipe);
}

/////////////////////////////////////////////////////////////////////////
// System Pipe Helpers Code
static inline SystemPipeSegment_t*
GetSystemPipeHead(
    _In_ SystemPipe_t*   Pipe)
{
    return atomic_load_explicit(&Pipe->ConsumerState.Head, memory_order_acquire);
}

static inline void
SetSystemPipeHead(
    _In_ SystemPipe_t*          Pipe,
    _In_ SystemPipeSegment_t*   Segment)
{
    assert((Pipe->Configuration & PIPE_MULTIPLE_CONSUMERS) == 0);
    atomic_store_explicit(&Pipe->ConsumerState.Head, Segment, memory_order_relaxed);
}

static inline SystemPipeSegment_t*
GetSystemPipeTail(
    _In_ SystemPipe_t*   Pipe)
{
    return atomic_load_explicit(&Pipe->ProducerState.Tail, memory_order_acquire);
}

static inline void
SetSystemPipeTail(
    _In_ SystemPipe_t*          Pipe,
    _In_ SystemPipeSegment_t*   Segment)
{
    assert((Pipe->Configuration & PIPE_MPMC) == 0);
    atomic_store_explicit(&Pipe->ProducerState.Tail, Segment, memory_order_release);
}

static inline _Bool
SwapSystemPipeTail(
    _In_ SystemPipe_t*          Pipe,
    _In_ SystemPipeSegment_t**  Segment,
    _In_ SystemPipeSegment_t*   Next)
{
    assert((Pipe->Configuration & PIPE_MPMC) != 0);
    return atomic_compare_exchange_strong_explicit(&Pipe->ProducerState.Tail, 
        Segment, Next, memory_order_release, memory_order_relaxed);
}

static inline _Bool
SwapSystemPipeHead(
    _In_ SystemPipe_t*          Pipe,
    _In_ SystemPipeSegment_t**  Segment,
    _In_ SystemPipeSegment_t*   Next)
{
    assert(Pipe->Configuration & PIPE_MULTIPLE_CONSUMERS);
    return atomic_compare_exchange_strong_explicit(&Pipe->ConsumerState.Head, 
        Segment, Next, memory_order_release, memory_order_acquire);
}

static inline unsigned int
GetSystemPipeProducerTicket(
    _In_ SystemPipe_t*   Pipe)
{
    if (Pipe->Configuration & PIPE_MULTIPLE_PRODUCERS) {
        return atomic_fetch_add_explicit(&Pipe->ProducerState.Ticket, 1, memory_order_acq_rel);
    }
    else {
        unsigned int Ticket = atomic_load_explicit(&Pipe->ProducerState.Ticket, memory_order_acquire);
        atomic_store_explicit(&Pipe->ProducerState.Ticket, Ticket + 1, memory_order_release);
        return Ticket;
    }
}

static inline unsigned int
GetSystemPipeConsumerTicket(
    _In_ SystemPipe_t*   Pipe)
{
    if (Pipe->Configuration & PIPE_MULTIPLE_PRODUCERS) {
        return atomic_fetch_add_explicit(&Pipe->ConsumerState.Ticket, 1, memory_order_acq_rel);
    }
    else {
        unsigned int Ticket = atomic_load_explicit(&Pipe->ConsumerState.Ticket, memory_order_acquire);
        atomic_store_explicit(&Pipe->ConsumerState.Ticket, Ticket + 1, memory_order_release);
        return Ticket;
    }
}

/////////////////////////////////////////////////////////////////////////
// System Pipe Buffer Code
static void
InitializeSegmentBuffer(
    _In_ SystemPipe_t*              Pipe,
    _In_ SystemPipeSegmentBuffer_t* Buffer)
{
    // If we use a SPSC queue then we can simplify and significantly speed up
    // the usage of the segment buffer. We use twice the lg size of entries. For 128
    // entries we thus have 32kb buffer, 256 entries we have 64kb.
    Buffer->Pointer         = (uint8_t*)kmalloc((1 << (Pipe->SegmentLgSize * 2)));
    Buffer->Size            = (1 << (Pipe->SegmentLgSize * 2));
    Buffer->TransferLimit   = ((Pipe->Configuration & PIPE_MPMC) == 0) ? 0 : Buffer->Size / 10;
    SlimSemaphoreConstruct(&Buffer->ReadQueue, 0, 0);
    SlimSemaphoreConstruct(&Buffer->WriteQueue, 0, 0);
    atomic_store(&Buffer->Credit, (1 << (Pipe->SegmentLgSize * 2)));
}

// Cost of writing to the segment buffer
static void
WithdrawSegmentBufferCredit(
    _In_ SystemPipeSegmentBuffer_t* Buffer,
    _In_ int                        Amount)
{
    int InitialCredit = atomic_fetch_sub(&Buffer->Credit, Amount);
    if ((InitialCredit - Amount) < Buffer->TransferLimit) {
        SlimSemaphoreWait(&Buffer->WriteQueue, 0);
    }
}

// Restores credit after reading from the segment buffer
static void
DepositSegmentBufferCredit(
    _In_ SystemPipeSegmentBuffer_t* Buffer,
    _In_ int                        Amount)
{
    int PreviousCredit = atomic_fetch_add(&Buffer->Credit, Amount);
    if ((PreviousCredit + Amount) >= Buffer->TransferLimit) {
        SlimSemaphoreSignal(&Buffer->WriteQueue, 1);
    }
}

// Adds debit, we try to consume more than we produce in some cases
static void
AddSegmentBufferDebit(
    _In_ SystemPipeSegmentBuffer_t* Buffer,
    _In_ int                        Amount)
{
    int PreviousDebit   = atomic_fetch_add(&Buffer->Debit, Amount);
    if ((PreviousDebit + Amount) > 0) {
        SlimSemaphoreWait(&Buffer->ReadQueue, 0);
    }
}

// Refunds the debit we owe, by producing things
static void
RefundSegmentBufferDebit(
    _In_ SystemPipeSegmentBuffer_t* Buffer,
    _In_ int                        Amount)
{
    int InitialDebit    = atomic_fetch_sub(&Buffer->Debit, Amount);
    if ((InitialDebit - Amount) <= 0) {
        SlimSemaphoreSignal(&Buffer->ReadQueue, 1);
    }
}

static unsigned int
AcquireSegmentBufferSpace(
    _In_ SystemPipeSegmentBuffer_t* Buffer,
    _In_ size_t                     Length)
{
    WithdrawSegmentBufferCredit(Buffer, (int)Length);
    return atomic_fetch_add(&Buffer->WritePointer, Length);
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

static void
WriteSegmentBuffer(
    _In_ SystemPipeSegmentBuffer_t* Buffer,
    _In_ const uint8_t*             Data,
    _In_ size_t                     Length)
{
    // Variables
    size_t BytesWritten = 0;
    size_t WriteIndex;
    assert(Buffer->TransferLimit == 0); // Only SPSC

    WithdrawSegmentBufferCredit(Buffer, (int)Length);
    WriteIndex = atomic_fetch_add(&Buffer->WritePointer, BytesWritten);
    while (BytesWritten < Length) {
        Buffer->Pointer[(WriteIndex++ & (Buffer->Size - 1))] = Data[BytesWritten++];
    }
    RefundSegmentBufferDebit(Buffer, (int)Length);
}

static void
ReadSegmentBuffer(
    _In_ SystemPipeSegmentBuffer_t* Buffer,
    _In_ uint8_t*                   Data,
    _In_ size_t                     Length)
{
    // Variables
    size_t BytesRead = 0;
    size_t ReadIndex;
    assert(Buffer->TransferLimit == 0); // Only SPSC
    
    AddSegmentBufferDebit(Buffer, (int)Length);
    ReadIndex = atomic_fetch_add(&Buffer->ReadPointer, Length);
    while (BytesRead < Length) {
        Data[BytesRead++] = Buffer->Pointer[(ReadIndex++ & (Buffer->Size - 1))];
    }
    DepositSegmentBufferCredit(Buffer, (int)Length);
}

/////////////////////////////////////////////////////////////////////////
// System Pipe Entry Code
static void
InitializeSegmentEntry(
    _In_ SystemPipeEntry_t* Entry)
{
    SlimSemaphoreConstruct(&Entry->SyncObject, 0, 1);
}

static SystemPipeEntry_t*
StartRetrievingSegmentEntryData(
    _In_ SystemPipeSegment_t*   Segment,
    _In_ unsigned int           Index)
{
    // Synchronize
    atomic_fetch_add(&Segment->References, 1);
    SlimSemaphoreWait(&Segment->Entries[Index].SyncObject, 0);
    return &Segment->Entries[Index];
}

static void
EndRetrievingSegmentEntryData(
    _In_ SystemPipeSegment_t*   Segment,
    _In_ SystemPipeEntry_t*     Entry)
{
    // Add credit and reduce references to segment
    atomic_fetch_add(&Segment->Buffer.ReadPointer, Entry->Length);
    DepositSegmentBufferCredit(&Segment->Buffer, (int)Entry->Length);
    atomic_fetch_sub(&Segment->References, 1);
}

static SystemPipeEntry_t*
AcquireSegmentEntry(
    _In_ SystemPipeSegment_t*   Segment,
    _In_ unsigned int           Index,
    _In_ size_t                 Length)
{
    unsigned int AcquiredIndex = AcquireSegmentBufferSpace(&Segment->Buffer, Length);
    Segment->Entries[Index].SegmentBufferIndex = AcquiredIndex;
    Segment->Entries[Index].SegmentBufferCurrentIndex = AcquiredIndex;
    Segment->Entries[Index].Length = Length;
    return &Segment->Entries[Index];
}

static void
MarkSegmentEntryComplete(
    _In_ SystemPipeEntry_t* Entry)
{
    Entry->SegmentBufferCurrentIndex = Entry->SegmentBufferIndex;
    SlimSemaphoreSignal(&Entry->SyncObject, 1);
}

/////////////////////////////////////////////////////////////////////////
// System Pipe Segment Code
static void
CreateSegment(
    _In_ SystemPipe_t*          Pipe,
    _In_ SystemPipeSegment_t**  Segment,
    _In_ unsigned int           TicketBase)
{
    // Variables
    SystemPipeSegment_t* Pointer;
    size_t BytesToAllocate = sizeof(SystemPipeSegment_t);
    int i;

    // Perform the allocation
    BytesToAllocate += sizeof(SystemPipeEntry_t) * TICKETS_PER_SEGMENT(Pipe);
    Pointer         = (SystemPipeSegment_t*)kmalloc(BytesToAllocate);
    assert(Pointer != NULL);
    memset((void*)Pointer, 0, sizeof(SystemPipeSegment_t));

    InitializeSegmentBuffer(Pipe, &Pointer->Buffer);
    Pointer->TicketBase = TicketBase;
    Pointer->Entries    = (SystemPipeEntry_t*)((uint8_t*)Pointer + sizeof(SystemPipeSegment_t));
    for (i = 0; i < TICKETS_PER_SEGMENT(Pipe); i++) {
        InitializeSegmentEntry(&Pointer->Entries[i]);
    }
    *Segment = Pointer;
}

static SystemPipeSegment_t*
GetNextSegment(
    _In_ SystemPipeSegment_t*   Segment)
{
    return atomic_load_explicit(&Segment->Link, memory_order_acquire);
}

static _Bool
SetNextSegment(
    _In_ SystemPipeSegment_t*   Segment,
    _In_ SystemPipeSegment_t*   Next)
{
    SystemPipeSegment_t* Expected = NULL;
    return atomic_compare_exchange_strong_explicit(&Segment->Link, &Expected, Next,
        memory_order_release, memory_order_relaxed);
}

static void
ReclaimSegment(
    _In_ SystemPipeSegment_t*   Segment)
{
    int References = atomic_fetch_sub(&Segment->References, 1);
    if (References == 1) {
        // No longer any threads are using this
        kfree(Segment);
    }
}

static SystemPipeSegment_t*
CreateNextSystemPipeSegment(
    _In_ SystemPipe_t*          Pipe,
    _In_ SystemPipeSegment_t*   Segment)
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
    _In_ SystemPipe_t*          Pipe,
    _In_ SystemPipeSegment_t*   Segment,
    _In_ unsigned int           Ticket)
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
    _In_ SystemPipe_t*          Pipe,
    _In_ SystemPipeSegment_t*   Segment,
    _In_ unsigned int           Ticket)
{
    while (Ticket >= (Segment->TicketBase + TICKETS_PER_SEGMENT(Pipe))) {
        Segment = GetNextSystemPipeSegment(Pipe, Segment, Ticket);
        assert(Segment != NULL);
    }
    return Segment;
}

/////////////////////////////////////////////////////////////////////////
// System Pipe SPSC Implementation

/* ReadSystemPipe
 * Performs raw reading that can only be used on pipes opened in SPSC mode. */
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
    
    // Handle SPSC differently
    if ((Pipe->Configuration & PIPE_MPMC) == 0) {
        ReadSegmentBuffer(&Segment->Buffer, Data, Length);
    }
    else {
        size_t LengthInEntry;
        AcquireSystemPipeConsumption(Pipe, &LengthInEntry, &State);
        ReadSystemPipeConsumption(&State, Data, LengthInEntry);
        FinalizeSystemPipeConsumption(Pipe, &State);
        Length = LengthInEntry;
    }
    return Length;
}

/* WriteSystemPipe
 * Performs raw writing that can only be used on pipes opened in SPSC mode. */
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

    // Handle SPSC differently
    if ((Pipe->Configuration & PIPE_MPMC) == 0) {
        WriteSegmentBuffer(&Segment->Buffer, Data, Length);
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
    _In_ SystemPipe_t*          Pipe,
    _In_ unsigned int           Ticket)
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
    _In_ SystemPipe_t*          Pipe,
    _In_ SystemPipeSegment_t*   Segment)
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

// Handle the cost of the production
static void
WithdrawProductionCredit(
    _In_ SystemPipe_t*          Pipe)
{
    int InitialCredit = atomic_fetch_sub(&Pipe->Credit, 1);
    if ((InitialCredit - 1) < Pipe->TransferLimit) {
        SlimSemaphoreWait(&Pipe->ProductionQueue, 0);
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
    assert(State != NULL);
    assert(Length > 0 && Length < UINT16_MAX);

    // Get tail for production
    Segment = GetSystemPipeTail(Pipe);
    Ticket  = GetSystemPipeProducerTicket(Pipe);
    if (Pipe->Configuration & PIPE_UNBOUNDED) {
        if (Pipe->Configuration & PIPE_MULTIPLE_PRODUCERS) {
            Segment = FindSystemPipeSegment(Pipe, Segment, Ticket);
        }
        Entry = AcquireSegmentEntry(Segment, TICKET_INDEX(Pipe, Ticket), Length);

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
        WithdrawProductionCredit(Pipe);
        Entry = AcquireSegmentEntry(Segment, TICKET_INDEX(Pipe, Ticket), Length);
    }

    // Update state
    State->Advance  = 0;
    State->Entry    = Entry;
    State->Segment  = Segment;
    return OsSuccess;
}

/* WriteSystemPipeProduction
 * Writes data into the production spot acquired. This spot is not marked
 * active before the amount of data written is equal to specfied in Acquire. */
size_t
WriteSystemPipeProduction(
    _In_ SystemPipeUserState_t* State,
    _In_ const uint8_t*         Data,
    _In_ size_t                 Length)
{
    // Variables
    size_t BytesWritten   = (State->Entry->SegmentBufferCurrentIndex - State->Entry->SegmentBufferIndex);
    size_t BytesAvailable = MIN(Length, State->Entry->Length - BytesWritten);
    assert(Data != NULL);

    if (BytesAvailable > 0) {
        WriteSegmentBufferSpace(&State->Segment->Buffer, Data, BytesAvailable, 
            &State->Entry->SegmentBufferCurrentIndex);
        if (State->Entry->Length == (State->Entry->SegmentBufferCurrentIndex - State->Entry->SegmentBufferIndex)) {
            MarkSegmentEntryComplete(State->Entry);
        }
    }
    return BytesAvailable;
}

/////////////////////////////////////////////////////////////////////////
// System Pipe Consumer Helper Implementations
static void
AdvanceSystemPipeConsumptionToHead(
    _In_ SystemPipe_t*          Pipe,
    _In_ unsigned int           Ticket)
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
    _In_ SystemPipe_t*          Pipe,
    _In_ SystemPipeSegment_t*   Segment)
{
    if ((Pipe->Configuration & PIPE_MPMC) == 0) {
        SystemPipeSegment_t *Next;
        while (GetSystemPipeTail(Pipe) == Segment) {
            CpuStall(1);
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

static void
DepositProductionCredit(
    _In_  SystemPipe_t*         Pipe)
{
    int InitialCredit = atomic_fetch_add(&Pipe->Credit, 1);
    if ((InitialCredit + 1) >= Pipe->TransferLimit) {
        SlimSemaphoreSignal(&Pipe->ProductionQueue, 1);
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
    // Variables
    SystemPipeSegment_t *Segment;
    unsigned int Ticket;
    assert(Pipe != NULL);
    assert(State != NULL);

    // Get head for consumption
    Segment         = GetSystemPipeHead(Pipe);
    Ticket          = GetSystemPipeConsumerTicket(Pipe);
    State->Advance  = 0;
    
    if (Pipe->Configuration & PIPE_UNBOUNDED) {
        if (Pipe->Configuration & PIPE_MULTIPLE_CONSUMERS) {
            Segment = FindSystemPipeSegment(Pipe, Segment, Ticket);
        }
        State->Entry = StartRetrievingSegmentEntryData(Segment, TICKET_INDEX(Pipe, Ticket));

        // Perform post-operations, they include making sure
        // we perform our maintience duties, like advancing the consumer
        if ((Ticket & (TICKETS_PER_SEGMENT(Pipe) - 1)) == (TICKETS_PER_SEGMENT(Pipe) - 1)) {
            State->Advance = 1;
        }
    }
    else {
        State->Entry = StartRetrievingSegmentEntryData(Segment, TICKET_INDEX(Pipe, Ticket));
    }
    State->Segment  = Segment;
    *Length         = State->Entry->Length;
    return OsSuccess;
}

/* ReadSystemPipeConsumption
 * Reads data into the provided buffer from production spot acquired. */
size_t
ReadSystemPipeConsumption(
    _In_ SystemPipeUserState_t* State,
    _In_ uint8_t*               Data,
    _In_ size_t                 Length)
{
    // Variables
    size_t BytesRead   = (State->Entry->SegmentBufferCurrentIndex - State->Entry->SegmentBufferIndex);
    size_t BytesAvailable = MIN(Length, State->Entry->Length - BytesRead);
    assert(Data != NULL);

    if (BytesAvailable > 0) {
        ReadSegmentBufferSpace(&State->Segment->Buffer, Data, BytesAvailable, 
            &State->Entry->SegmentBufferCurrentIndex);
    }
    return BytesAvailable;
}

/* FinalizeSystemPipeConsumption
 * Finalizes the consume-process by performing maintience tasks that were assigned
 * for the given entry consumed. */
void
FinalizeSystemPipeConsumption(
    _In_ SystemPipe_t*          Pipe,
    _In_ SystemPipeUserState_t* State)
{
    assert(Pipe != NULL);
    assert(State != NULL);
    EndRetrievingSegmentEntryData(State->Segment, State->Entry);
    if (Pipe->Configuration & PIPE_UNBOUNDED) {
        if (State->Advance) {
            AdvanceSystemPipeConsumer(Pipe, State->Segment);
        }
    }
    else {
        DepositProductionCredit(Pipe);
    }
}

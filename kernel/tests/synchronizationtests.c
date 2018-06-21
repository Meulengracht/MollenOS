/* MollenOS
 *
 * Copyright 2011 - 2018, Philip Meulengracht
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
 * OS Testing Suite
 *  - Synchronization tests to verify the robustness and correctness of concurrency.
 */
#define __MODULE "TEST"
#define __TRACE

#include <scheduler.h>
#include <threading.h>
#include <assert.h>
#include <debug.h>
#include <pipe.h>
#include <heap.h>

static volatile int SynchronizationTestActive   = 0;
static int SynchronizationConsumes              = 0;
static int SynchronizationProduces[2]           = { 0 };
static atomic_int SynchronizationId             = ATOMIC_VAR_INIT(0);

/* TestWorkerConsumer
 * The consumer thread for performing piping operations. */
void
TestWorkerConsumer(void* Context)
{
    // Variables
    MRemoteCall_t Message;
    SystemPipe_t *CommPipe  = (SystemPipe_t*)Context;

    while (SynchronizationTestActive) {
        ReadSystemPipe(CommPipe, (uint8_t*)&Message, sizeof(MRemoteCall_t));
        SynchronizationConsumes++;
    }
    TRACE(" > Exiting consumer, final count %u", SynchronizationConsumes);
}

/* TestWorkerProducer
 * The producer thread for performing piping operations. */
void
TestWorkerProducer(void* Context)
{
    // Variables
    MRemoteCall_t Message;
    
    SystemPipe_t *CommPipe  = (SystemPipe_t*)Context;
    int Id                  = atomic_fetch_add(&SynchronizationId, 1);

    while (SynchronizationTestActive) {
        WriteSystemPipe(CommPipe, (uint8_t*)&Message, sizeof(MRemoteCall_t));
        SynchronizationProduces[Id]++;
    }
    TRACE(" > Exiting producer, final count %u", SynchronizationProduces[Id]);
}

/* TestSynchronization
 * Performs all the synchronization tests in the system. */
void
TestSynchronization(void *Unused)
{
    // Variables
    SystemPipe_t *CommPipe;
    UUId_t Threads[3];
    int Timeout;
    _CRT_UNUSED(Unused);

    // Debug
    TRACE("TestSynchronization()");

    // Create the communication pipe of a default sizes with MPMC configuration 
    CommPipe = CreateSystemPipe(PIPE_MULTIPLE_PRODUCERS, PIPE_DEFAULT_ENTRYCOUNT);
    //CommPipe = CreateSystemPipe(PIPE_MPMC | PIPE_STRUCTURED_BUFFER, PIPE_DEFAULT_ENTRYCOUNT);
    assert(CommPipe != NULL);
    SynchronizationTestActive = 1;

    // Start two threads, let them run for a few minutes to allow a thorough testing
    // of the pipes.
    Threads[0] = ThreadingCreateThread("Test_Consumer", TestWorkerConsumer, CommPipe, 0);
    Threads[1] = ThreadingCreateThread("Test_Producer", TestWorkerProducer, CommPipe, 0);
    Threads[2] = ThreadingCreateThread("Test_Producer", TestWorkerProducer, CommPipe, 0);
    assert(Threads[0] != UUID_INVALID);
    assert(Threads[1] != UUID_INVALID);
    assert(Threads[2] != UUID_INVALID);

    TRACE(" > Waiting 2 minutes before terminating threads");
    Timeout = 120 * 1000;
    while (Timeout > 0) {
        SchedulerThreadSleep(NULL, 10 * 1000);
        Timeout -= (10 * 1000);
        TRACE(" > Consumes %u / Produces %u : %u", SynchronizationConsumes, SynchronizationProduces[0], SynchronizationProduces[1]);
        TRACE(" > Buffer R/W: (%u/%u), CR/CW: (%u/%u), ReadQueue %i, WriteQueue %i ", 
            atomic_load(&CommPipe->ConsumerState.Head->Buffer.ReadPointer),     atomic_load(&CommPipe->ConsumerState.Head->Buffer.WritePointer),
            atomic_load(&CommPipe->ConsumerState.Head->Buffer.ReadCommitted),   atomic_load(&CommPipe->ConsumerState.Head->Buffer.WriteCommitted),
            atomic_load(&CommPipe->ConsumerState.Head->Buffer.ReadQueue.Value), atomic_load(&CommPipe->ConsumerState.Head->Buffer.WriteQueue.Value));
    }
    SynchronizationTestActive = 0;
}

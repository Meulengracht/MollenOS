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

// Pipe indices
#define TEST_CONSUMER    0
#define TEST_PRODUCER    1

// Configuration Flags
#define SYNCHRONIZATION_TEST_DUBLEX         0x1
#define SYNCHRONIZATION_TEST_CONSUMER       0x2

struct SynchTestPackage {
    Flags_t         Configuration;
    SystemPipe_t*   Pipes[2];
    int             Consumptions[2];
    int             Productions[2];
};

static atomic_int ConsumptionId = ATOMIC_VAR_INIT(0);
static atomic_int ProductionId  = ATOMIC_VAR_INIT(0);

/* ConsumeWorker
 * The consumer thread for performing piping operations. */
void
ConsumeWorker(void* Context)
{
    // Variables
    struct SynchTestPackage *Package = (struct SynchTestPackage*)Context;
    int Id = atomic_fetch_add(&ConsumptionId, 1);
    MRemoteCall_t Message;

    while (1) {
        ReadSystemPipe(Package->Pipes[TEST_CONSUMER], (uint8_t*)&Message, sizeof(MRemoteCall_t));
        Package->Consumptions[Id]++;
        if (Package->Configuration & SYNCHRONIZATION_TEST_DUBLEX) {
            WriteSystemPipe(Package->Pipes[TEST_PRODUCER], (uint8_t*)&Message, sizeof(MRemoteCall_t));
        }
    }
}

/* ProduceWorker
 * The consumer thread for performing piping operations. */
void
ProduceWorker(void* Context)
{
    // Variables
    struct SynchTestPackage *Package = (struct SynchTestPackage*)Context;
    int Id = atomic_fetch_add(&ProductionId, 1);
    MRemoteCall_t Message;

    while (1) {
        WriteSystemPipe(Package->Pipes[TEST_CONSUMER], (uint8_t*)&Message, sizeof(MRemoteCall_t));
        Package->Productions[Id]++;
        if (Package->Configuration & SYNCHRONIZATION_TEST_DUBLEX) {
            ReadSystemPipe(Package->Pipes[TEST_PRODUCER], (uint8_t*)&Message, sizeof(MRemoteCall_t));
        }
    }
}

/* WaitForSynchronizationTest
 * Waits for the synchronization test to run for <timeout> in <steps>.
 * Prints debug information every step time. */
void
WaitForSynchronizationTest(
    _In_ struct SynchTestPackage*   Package,
    _In_ UUId_t*                    Threads,
    _In_ int                        NumberOfThreads, 
    _In_ size_t                     Timeout,
    _In_ size_t                     Step)
{
    size_t TimeLeft = Timeout;
    int i;

    // Run the test in steps
    while (TimeLeft > 0) {
        SchedulerThreadSleep(NULL, Step);
        TimeLeft -= Step;

        // Debug the number of iterations done by each worker
        if (NumberOfThreads == 4) {
            TRACE(" > consumes %u : %u / produces %u : %u", Package->Consumptions[0],
                Package->Consumptions[1], Package->Productions[0], Package->Productions[1]);
        }
        else if (NumberOfThreads == 3) {
            TRACE(" > consumes %u / produces %u : %u", Package->Consumptions[0],
                Package->Productions[0], Package->Productions[1]);
        }
        else {
            TRACE(" > consumes %u / produces %u", Package->Consumptions[0], Package->Productions[0]);
        }

        // Debug data for simplex pipe
        TRACE(" > consumer pipe: buffer r/w: (%u/%u), cr/cw: (%u/%u)", 
            atomic_load(&Package->Pipes[TEST_CONSUMER]->ConsumerState.Head->Buffer.ReadPointer),
            atomic_load(&Package->Pipes[TEST_CONSUMER]->ConsumerState.Head->Buffer.WritePointer),
            atomic_load(&Package->Pipes[TEST_CONSUMER]->ConsumerState.Head->Buffer.ReadCommitted),
            atomic_load(&Package->Pipes[TEST_CONSUMER]->ConsumerState.Head->Buffer.WriteCommitted));
        
        // Debug data for dublex pipe
        if (Package->Configuration & SYNCHRONIZATION_TEST_DUBLEX) {
            TRACE(" > producer pipe: buffer r/w: (%u/%u), cr/cw: (%u/%u)", 
                atomic_load(&Package->Pipes[TEST_PRODUCER]->ConsumerState.Head->Buffer.ReadPointer),
                atomic_load(&Package->Pipes[TEST_PRODUCER]->ConsumerState.Head->Buffer.WritePointer),
                atomic_load(&Package->Pipes[TEST_PRODUCER]->ConsumerState.Head->Buffer.ReadCommitted),
                atomic_load(&Package->Pipes[TEST_PRODUCER]->ConsumerState.Head->Buffer.WriteCommitted));
        }
    }

    // Cleanup by killing threads and freeing pipes
    TRACE(" > killing threads");
    for (i = 0; i < NumberOfThreads; i++) {
        if (ThreadingTerminateThread(Threads[i], 0, 1) != OsSuccess) {
            WARNING(" > failed to kill thread %u", Threads[i]);
        }
    }
    
    TRACE(" > cleaning up");
    DestroySystemPipe(Package->Pipes[TEST_CONSUMER]);
    if (Package->Configuration & SYNCHRONIZATION_TEST_DUBLEX) {
        DestroySystemPipe(Package->Pipes[TEST_PRODUCER]);
    }
    atomic_store_explicit(&ConsumptionId, 0, memory_order_relaxed);
    atomic_store_explicit(&ProductionId, 0, memory_order_relaxed);
    memset((void*)Package, 0, sizeof(struct SynchTestPackage));
}

/* TestSynchronization
 * Performs all the synchronization tests in the system. */
void
TestSynchronization(void *Unused)
{
    // Variables
    struct SynchTestPackage *Package;
    UUId_t Threads[4];
    _CRT_UNUSED(Unused);

    // Debug
    TRACE("TestSynchronization()");

    // Allocate the packages on heap, stack is not OK
    Package = (struct SynchTestPackage*)kmalloc(sizeof(struct SynchTestPackage));
    memset((void*)Package, 0, sizeof(struct SynchTestPackage));

    /////////////////////////////////////////////////////////////////////////////////
    // Test 1
    // Test the basic raw pipe communication with a dublex connection, SPSC mode
    /////////////////////////////////////////////////////////////////////////////////
    TRACE(" > running configuration (RAW, SPSC, DUBLEX)");

    // Setup package
    Package->Configuration          = SYNCHRONIZATION_TEST_DUBLEX;
    Package->Pipes[TEST_CONSUMER]   = CreateSystemPipe(0, 6);
    Package->Pipes[TEST_PRODUCER]   = CreateSystemPipe(0, 6);

    // Check pipes
    assert(Package->Pipes[TEST_CONSUMER] != NULL);
    assert(Package->Pipes[TEST_PRODUCER] != NULL);

    // Spawn threads
    Threads[0] = ThreadingCreateThread("Test_Consumer", ConsumeWorker, Package, 0);
    Threads[1] = ThreadingCreateThread("Test_Producer", ProduceWorker, Package, 0);
    assert(Threads[0] != UUID_INVALID);
    assert(Threads[1] != UUID_INVALID);
    WaitForSynchronizationTest(Package, Threads, 2, 120 * 1000, 10 * 1000);

    /////////////////////////////////////////////////////////////////////////////////
    // Test 2
    // Test the basic raw pipe communication with a simplex connection, SPSC mode
    /////////////////////////////////////////////////////////////////////////////////
    TRACE(" > running configuration (RAW, SPSC, SIMPLEX)");

    // Setup package
    Package->Configuration          = 0;
    Package->Pipes[TEST_CONSUMER]   = CreateSystemPipe(0, 6);

    // Check pipes
    assert(Package->Pipes[TEST_CONSUMER] != NULL);

    // Spawn threads
    Threads[0] = ThreadingCreateThread("Test_Consumer", ConsumeWorker, Package, 0);
    Threads[1] = ThreadingCreateThread("Test_Producer", ProduceWorker, Package, 0);
    assert(Threads[0] != UUID_INVALID);
    assert(Threads[1] != UUID_INVALID);
    WaitForSynchronizationTest(Package, Threads, 2, 120 * 1000, 10 * 1000);

    /////////////////////////////////////////////////////////////////////////////////
    // Test 3
    // Test the structured pipe communication with a dublex connection, SPSC mode
    /////////////////////////////////////////////////////////////////////////////////
    TRACE(" > running configuration (STRUCTURED, SPSC, DUBLEX)");

    // Setup package
    Package->Configuration          = SYNCHRONIZATION_TEST_DUBLEX;
    Package->Pipes[TEST_CONSUMER]   = CreateSystemPipe(PIPE_STRUCTURED_BUFFER, PIPE_DEFAULT_ENTRYCOUNT);
    Package->Pipes[TEST_PRODUCER]   = CreateSystemPipe(PIPE_STRUCTURED_BUFFER, PIPE_DEFAULT_ENTRYCOUNT);

    // Check pipes
    assert(Package->Pipes[TEST_CONSUMER] != NULL);
    assert(Package->Pipes[TEST_PRODUCER] != NULL);

    // Spawn threads
    Threads[0] = ThreadingCreateThread("Test_Consumer", ConsumeWorker, Package, 0);
    Threads[1] = ThreadingCreateThread("Test_Producer", ProduceWorker, Package, 0);
    assert(Threads[0] != UUID_INVALID);
    assert(Threads[1] != UUID_INVALID);
    WaitForSynchronizationTest(Package, Threads, 2, 120 * 1000, 10 * 1000);

    /////////////////////////////////////////////////////////////////////////////////
    // Test 4
    // Test the structured pipe communication with a simplex connection, SPSC mode
    /////////////////////////////////////////////////////////////////////////////////
    TRACE(" > running configuration (STRUCTURED, SPSC, SIMPLEX)");

    // Setup package
    Package->Configuration          = 0;
    Package->Pipes[TEST_CONSUMER]   = CreateSystemPipe(PIPE_STRUCTURED_BUFFER, PIPE_DEFAULT_ENTRYCOUNT);

    // Check pipes
    assert(Package->Pipes[TEST_CONSUMER] != NULL);

    // Spawn threads
    Threads[0] = ThreadingCreateThread("Test_Consumer", ConsumeWorker, Package, 0);
    Threads[1] = ThreadingCreateThread("Test_Producer", ProduceWorker, Package, 0);
    assert(Threads[0] != UUID_INVALID);
    assert(Threads[1] != UUID_INVALID);
    WaitForSynchronizationTest(Package, Threads, 2, 120 * 1000, 10 * 1000);
}

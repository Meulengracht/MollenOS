/**
 * Copyright 2022, Philip Meulengracht
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
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include <ddk/ddkdefs.h> // for __reserved
#include <os/futex.h>
#include <internal/_tls.h>
#include <internal/_utils.h>
#include <internal/_syscalls.h>
#include <os/usched/usched.h>
#include <os/usched/xunit.h>
#include <signal.h>
#include "private.h"

// Needed to handle thread stuff now on a userspace basis
CRTDECL(void, __cxa_threadinitialize(void));
CRTDECL(void, __cxa_threadfinalize(void));

// NOTES
// TLS/CRT must now be done PER user thread
// This means we need to switch values in the GS register
// on each thread to make sure that the register point to
// the correct TLS each time.

struct execution_manager {
    struct usched_execution_unit primary;
    int                          count;
    mtx_t                        queue_lock;
};

static struct execution_manager g_executionManager = { 0 };

static void __handle_unit_signal(int sig)
{
    if (sig == SIGUSR1) {
        // Only request exit, we want to finish executing the current job, but
        // we mark the current job as cancelled.
        __usched_xunit_tls_current()->exit_requested = 1;
        usched_task_cancel_current();
    } else if (sig == SIGUSR2) {
        // New task has been scheduled for us, we need to move it into the ready queue
    }
}

void usched_xunit_init(void)
{
    // initialize the manager
    mtx_init(&g_executionManager.queue_lock, mtx_plain);

    // initialize the primary xunit
    mtx_init(&g_executionManager.primary.tls.wait_queue.lock);
}

static int __wait(struct usched_execution_unit* unit)
{
    FutexParameters_t params = {
            ._futex0 = &unit->sync,
            ._val0   = 0,
            ._flags  = FUTEX_WAIT_PRIVATE,
    };

    int currentValue = atomic_exchange(&unit->sync, 0);
    if (currentValue) {
        // changes pending, lets go
        return EINTR;
    }

    oserr_t oserr = Syscall_FutexWait(&params);
    if (oserr == OsInterrupted) {
        return EINTR;
    }
    return EOK;
}

static void __load_wait_queue(struct usched_execution_unit* unit)
{
    struct usched_job* job;

    mtx_lock(&unit->tls.wait_queue.lock);
    job = unit->tls.wait_queue.next;
    unit->tls.wait_queue.next = NULL;
    mtx_unlock(&unit->tls.wait_queue.lock);

    if (job != NULL) {
        __usched_append_job(&unit->scheduler.ready, job);
    }
}

void usched_xunit_start(usched_task_fn startFn, void* argument)
{
    struct usched_execution_unit* unit = &g_executionManager.primary;
    void*                         mainCT;

    // Install the execution unit specific data into slot 2. This will then
    // be available to all units, and gives us an opportunity to store things
    // like the current scheduler etc.
    __set_reserved(2, (uintptr_t)&unit->tls);

    // Install a signal handler in case we need to be told something
    signal(SIGUSR1, __handle_unit_signal);
    signal(SIGUSR2, __handle_unit_signal);

    // Initialize the userspace thread systems before going into the main loop
    usched_init();

    // Queue the first task, this would most likely be the introduction to 'main' or anything
    // like that, we don't really use the CT token, but just capture it for warnings.
    mainCT = usched_task_queue(startFn, argument);
    while (1) {
        int timeout;

        do {
            timeout = usched_yield();
        } while (!unit->tls.exit_requested && timeout == 0);

        if (unit->tls.exit_requested) {
            break;
        }

        // Wait now for new tasks to enter the ready queue
        __wait(unit);
        __load_wait_queue(unit);
    }

    // Cleanup systems, destroy children, etc etc
}

int usched_xunit_count(void)
{
    if (g_executionManager.count == 0) {
        errno = ENODEV;
        return -1;
    }
    return g_executionManager.count;
}

struct execution_unit_tls* __usched_xunit_tls_current(void) {
    return (struct execution_unit_tls*)__get_reserved(2);
}

static void __execution_unit_main(void* data)
{
    struct usched_execution_unit* unit = data;
    struct thread_storage         tls;

    // Initialize the thread storage system for the execution unit,
    // each execution unit has their own TLS as well
    __tls_initialize(&tls);
    __tls_switch(&tls);

    // Install the execution unit specific data into slot 2. This will then
    // be available to all units, and gives us an opportunity to store things
    // like the current scheduler etc.
    __set_reserved(2, (uintptr_t)&unit->tls);

    // Run any C/C++ initialization for the execution unit. Before this call
    // the tls must be set correctly.
    __cxa_threadinitialize();

    // Install a signal handler in case we need to be told something
    signal(SIGUSR1, __handle_unit_signal);
    signal(SIGUSR2, __handle_unit_signal);

    // Initialize the userspace thread systems before going into the main loop
    usched_init();

    // Enter main loop, this loop is differnt from the primary execution unit, as shutdown is
    // not handled by the child execution units. If we run out of tasks, we just sleep untill one
    // becomes available. If the primary runs out of tasks, and all XU's sleep, then the program
    // exits.
    while (1) {
        int timeout;

        do {
            timeout = usched_yield();
        } while (!unit->tls.exit_requested && timeout == 0);

        if (unit->tls.exit_requested) {
            break;
        }

        // wait for any pending changes to our system, then we
        // transfer all queued tasks to our ready queue
        __wait(unit);
        __load_wait_queue(unit);
    }

    // Run deinit of C/C++ handlers for the execution unit thread
    __cxa_threadfinalize();
    __tls_destroy(&tls);

    // Exit thread, but let's keep a catch-all in case of fire
    Syscall_ThreadExit(0);
    for(;;);
}

static int __create_execution_unit(void)
{

}

int usched_xunit_set_count(int count)
{

}

static bool __bit_set(const unsigned int* mask, int bit)
{
    unsigned int block  = bit / (unsigned int)((sizeof(unsigned int) * 8));
    unsigned int offset = bit % (unsigned int)((sizeof(unsigned int) * 8));
    return (mask[block] & offset) > 0;
}

static struct usched_execution_unit* __select_lowest_loaded(unsigned int* mask)
{
    struct usched_execution_unit* xunit;
    struct usched_execution_unit* result = NULL;
    int                           id;

    xunit = &g_executionManager.primary;
    id    = 0;
    while (xunit != NULL) {
        if (__bit_set(mask, id)) {
            if (result == NULL || result->load > xunit->load) {
                result = xunit;
            }
        }

        xunit = xunit->next;
        id++;
    }

    return result;
}

int __usched_xunit_queue_job(struct usched_job* job, struct usched_job_paramaters* params)
{
    struct usched_execution_unit* xunit;

    mtx_lock(&g_executionManager.queue_lock);
    xunit = __select_lowest_loaded(params->affinity_mask);
    if (!xunit) {
        mtx_unlock(&g_executionManager.queue_lock);
        return -1;
    }

    // add it to the scheduler for that thread
    xunit->load += params->job_weight;
    mtx_unlock(&g_executionManager.queue_lock);

    // If we are on the correct execution unit, we can just add it to the ready
    // queue and skip any additional handling right there. The tricky part is if
    // we need to move this job to another execution unit
    if (&xunit->tls == __usched_xunit_tls_current()) {
        __usched_append_job(&xunit->scheduler.ready, job);
        return 0;
    }

    mtx_lock(&xunit->tls.wait_queue.lock);
    __usched_append_job(&xunit->tls.wait_queue.next, job);
    mtx_unlock(&xunit->tls.wait_queue.lock);

    // signal the thread
    atomic_fetch_add(&xunit->scheduler.pending, 1);
    thrd_signal(xunit->thread_id, SIGUSR2);
    return 0;
}

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
#include <os/threads.h>
#include <internal/_tls.h>
#include <internal/_syscalls.h>
#include <os/usched/usched.h>
#include <os/usched/xunit.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include "private.h"

// Needed to handle thread stuff now on userspace basis
CRTDECL(void, __cxa_threadinitialize(void));
CRTDECL(void, __cxa_threadfinalize(void));

// NOTES
// TLS/CRT must now be done PER user thread
// This means we need to switch values in the GS register
// on each thread to make sure that the register point to
// the correct TLS each time.

struct execution_manager {
    struct usched_execution_unit  primary;
    struct usched_execution_unit* detached;
    int                           count;
    mtx_t                         lock;
    int                           core_count;
};

static struct execution_manager g_executionManager = { 0 };

static void __execution_unit_exit(void)
{
    struct thread_storage* tls = __tls_current();

    if (__usched_prepare_migrate()) {
        return; // return from migrated thread
    }

    // Run deinit of C/C++ handlers for the execution unit thread
    __cxa_threadfinalize();
    __tls_destroy(tls);

    // Exit thread, but let's keep a catch-all in case of fire
    Syscall_ThreadExit(0);
    for(;;);
}

static void __handle_unit_signal(int sig)
{
    if (sig == SIGUSR1) {
        __execution_unit_exit();
    }
}

static void __execution_unit_tls_construct(struct execution_unit_tls* tls, struct usched_scheduler* scheduler)
{
    tls->scheduler = scheduler;
}

static void __execution_unit_construct(struct usched_execution_unit* unit)
{
    unit->thread_id = UUID_INVALID;
    __execution_unit_tls_construct(&unit->tls, &unit->scheduler);
}

static int __get_cpu_count(void)
{
    SystemDescriptor_t descriptor;
    SystemQuery(&descriptor);
    return (int)descriptor.NumberOfActiveCores;
}

void usched_xunit_init(void)
{
    // initialize the manager
    mtx_init(&g_executionManager.lock, mtx_recursive);
    g_executionManager.count = 1;
    g_executionManager.core_count = __get_cpu_count();

    // initialize the primary xunit
    __execution_unit_construct(&g_executionManager.primary);
    g_executionManager.primary.thread_id = thrd_current();

    // Install the execution unit specific data into slot 2. This will then
    // be available to all units, and gives us an opportunity to store things
    // like the current scheduler etc.
    __set_reserved(2, (uintptr_t)&g_executionManager.primary.tls);

    // Initialize all the subsystems for the primary execution unit. Each unit needs
    // to run this
    __usched_init(&g_executionManager.primary.scheduler, &(struct usched_init_params) {
        .detached_job = NULL
    });
}

_Noreturn void usched_xunit_main_loop(usched_task_fn startFn, void* argument)
{
    struct timespec deadline;
    void*           mainCT;

    // Queue the first task, this would most likely be the introduction to 'main' or anything
    // like that, we don't really use the CT token, but just capture it for warnings.
    mainCT = usched_task_queue(startFn, argument);
    (void)mainCT; // TODO: better support for cancelling tasks...
    while (1) {
        int status;

        do {
            status = usched_yield(&deadline);
        } while (status == 0);

        // Wait now for new tasks to enter the ready queue. If errno is set
        // to EWOULDBLOCK, this means we should wait until the deadline is
        // reached.
        if (errno == EWOULDBLOCK) { usched_timedwait(&deadline); }
        else                      { usched_wait(); }
    }
}

int usched_xunit_count(void)
{
    if (g_executionManager.count == 0) {
        errno = ENOSYS;
        return -1;
    }
    return g_executionManager.count;
}

struct execution_unit_tls* __usched_xunit_tls_current(void) {
    return (struct execution_unit_tls*)__get_reserved(2);
}

_Noreturn static void __execution_unit_main(void* data)
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

    // Initialize the userspace thread systems before going into the main loop
    __usched_init(&unit->scheduler, &(struct usched_init_params) {
        .detached_job = unit->params.detached_job
    });

    // Enter main loop, this loop is different from the primary execution unit, as shutdown is
    // not handled by the child execution units. If we run out of tasks, we just sleep untill one
    // becomes available. If the primary runs out of tasks, and all XU's sleep, then the program
    // exits.
    while (1) {
        struct timespec deadline;
        int             status;

        do {
            status = usched_yield(&deadline);
        } while (status == 0);

        // Wait now for new tasks to enter the ready queue. If errno is set
        // to EWOULDBLOCK, this means we should wait until the deadline is
        // reached.
        if (errno == EWOULDBLOCK) { usched_timedwait(&deadline); }
        else                      { usched_wait(); }
    }
}

static struct usched_execution_unit* __execution_unit_new(void)
{
    struct usched_execution_unit* unit;

    unit = malloc(sizeof(struct usched_execution_unit));
    if (unit == NULL) {
        return NULL;
    }
    memset(unit, 0, sizeof(struct usched_execution_unit));
    __execution_unit_construct(unit);
    return unit;
}

static int __spawn_execution_unit(struct usched_execution_unit* unit, unsigned int* affinityMask)
{
    ThreadParameters_t parameters;
    oserr_t            oserr;

    // Use default thread parameters for now until we decide on another
    // course of action.
    ThreadParametersInitialize(&parameters);
    //ThreadParametersSetAffinityMask(affinityMask);

    // Spawn a thread in the raw fashion to allow us to control the CRT initalization
    // a bit more fine-grained as we want to inject another per-thread value.
    oserr = Syscall_ThreadCreate(
            (thrd_start_t)__execution_unit_main,
            unit, &parameters,
            &unit->thread_id
    );
    return OsErrToErrNo(oserr);
}

static void __execution_unit_delete(struct usched_execution_unit* unit)
{
    // We cannot destroy the scheduler here, it was initialized on-thread and
    // must be destroyed by the same thread upon exit
    free(unit);
}

static int __start_execution_unit(unsigned int* affinityMask, struct usched_job* detachedJob)
{
    struct usched_execution_unit* unit = __execution_unit_new();
    struct usched_execution_unit* i    = NULL;
    int                           status;

    if (unit == NULL) {
        return -1;
    }

    // update spawn parameters
    unit->params.detached_job = detachedJob;

    status = __spawn_execution_unit(unit, affinityMask);
    if (status) {
        __execution_unit_delete(unit);
        return status;
    }

    // add it to the list of execution units
    if (detachedJob != NULL) {
        if (g_executionManager.detached == NULL) {
            g_executionManager.detached = unit;
        } else {
            i = g_executionManager.detached;
        }
    } else {
        i = &g_executionManager.primary;
        g_executionManager.count++;
    }

    if (i != NULL) {
        while (i->next) {
            i = i->next;
        }
        i->next = unit;
    }
    return 0;
}

static int __stop_execution_unit(void)
{
    struct usched_execution_unit* unit;
    int                           unitResult;

    // Check if there are any available execution units to kill,
    // otherwise we just return here.
    if (g_executionManager.primary.next == NULL) {
        errno = ENOENT;
        return -1;
    }

    // remove it, and reduce count, then we handle cleanup
    unit = g_executionManager.primary.next;
    g_executionManager.primary.next = unit->next;
    g_executionManager.count--;

    // We are bound to kill ourselves at some point, so we instead return
    // with ESHUTDOWN to make sure that the killing of us, takes place after
    // we've killed others.
    if (&unit->tls == __usched_xunit_tls_current()) {
        // uh oh, we need to kill ourselves
        errno = ESHUTDOWN;
        return -1;
    }

    // signal the execution unit to stop, wait for it to terminate,
    // and then we transfer its tasks to the remaining units
    thrd_signal(unit->thread_id, SIGUSR1);
    thrd_join(unit->thread_id, &unitResult);
    __execution_unit_delete(unit);
    return 0;
}

int usched_xunit_set_count(int count)
{
    int killCurrentXUnit = 0;
    int result           = 0;

    // plz call _start first
    if (g_executionManager.count == 0) {
        errno = ENOSYS;
        return -1;
    }

    // can't set it to 0
    if (count == 0) {
        errno = EINVAL;
        return -1;
    }

    if (count < 0) {
        count = g_executionManager.core_count;
    }

    mtx_lock(&g_executionManager.lock);
    if (count > g_executionManager.count) {
        int xunitsToCreate = count - g_executionManager.count;
        for (int i = 0; i < xunitsToCreate; i++) {
            result = __start_execution_unit(NULL, NULL);
            if (result) {
                break;
            }
        }
    } else if (count < g_executionManager.count) {
        int xunitsToStop = g_executionManager.count - count;
        for (int i = 0; i < xunitsToStop; i++) {
            result = __stop_execution_unit();
            if (result) {
                // Two types of error here, either it's because
                // we are putting off suicide (ESHUTDOWN), or it's
                // because there are no more units to kill (ENOENT)
                if (errno == ESHUTDOWN) {
                    killCurrentXUnit = 1;
                } else {
                    // Simply break out as we do not need to do anymore.
                    break;
                }
            }
        }
    }
    mtx_unlock(&g_executionManager.lock);

    // Before returning - which we should not do if we need to kill
    // the current thread, we just exit
    if (killCurrentXUnit) {
        __execution_unit_exit();
    }
    return result;
}

int __xunit_start_detached(struct usched_job* job, struct usched_job_parameters* params)
{
    int result;

    // Create a new execution unit, mark it RUNNING_DETACHED. We then supply it the
    // job it will be executing. Make sure we proxy the affinity mask for the execution
    // unit in case the job is requesting a specific core to run on.
    mtx_lock(&g_executionManager.lock);
    result = __start_execution_unit(params->affinity_mask, job);
    mtx_unlock(&g_executionManager.lock);
    if (result) {
        return result;
    }
    return 0;
}

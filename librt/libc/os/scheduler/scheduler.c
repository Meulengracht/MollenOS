/**
 * Copyright 2021, Philip Meulengracht
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
 * User threads implementation. Implements support for multiple tasks running entirely
 * in userspace. This is supported by additional synchronization primitives in the usched_
 * namespace.
 */

#include <errno.h>
#include <os/usched/usched.h>
#include <setjmp.h>
#include <string.h>
#include <stdlib.h>
#include <threads.h>
#include "private.h"

static struct usched_scheduler g_scheduler;

struct usched_scheduler* __usched_get_scheduler(void) {
    return &g_scheduler;
}

void usched_init(void)
{
    if (g_scheduler.magic == SCHEDULER_MAGIC) {
        return;
    }

    // initialize the global scheduler, there will always only exist one
    // scheduler instance, unless we implement multiple executors
    memset(&g_scheduler, 0, sizeof(struct usched_scheduler));
    mtx_init(&g_scheduler.lock, mtx_plain);
    g_scheduler.magic = SCHEDULER_MAGIC;
}

static struct usched_job* GetNextReady(struct usched_scheduler* scheduler)
{
    struct usched_job* next = scheduler->ready;

    if (!scheduler->ready) {
        return NULL;
    }

    scheduler->ready = next->next;
    next->next = NULL;
    return next;
}

void TaskMain(struct usched_job* job)
{
    job->state = JobState_RUNNING;
    job->entry(job->argument, job);
    job->state = JobState_FINISHING;
    usched_yield();
}

static void SwitchTask(struct usched_job* current, struct usched_job* next)
{
    char* stack;

    // save the current context and set a return point
    if (current) {
        if (setjmp(current->context)) {
            return;
        }
    }

    // if the thread we want to switch to already has a valid jmp_buf then
    // we can just longjmp into that context
    if (next->state != JobState_CREATED) {
        longjmp(next->context, 1);
    }

    // First time we initalize a context we must manually switch the stack
    // pointer and call the correct entry.
    stack = (char*)next->stack + next->stack_size;
#if defined(__amd64__)
    __asm__ (
            "movq %0, %%rcx; movq %1, %%rsp; callq TaskMain\n"
            :: "r"(next), "r"(stack)
            : "rdi", "rsp", "memory");
#elif defined(__i386__)
    __asm__ (
            "push %0, %%eax; movd %1, %%esp; push %%eax; call TaskMain\n"
            :: "r"(next), "r"(stack)
            : "eax", "esp", "memory");
#else
#error "Unimplemented architecture for userspace scheduler"
#endif
}

static void TaskDestroy(struct usched_job* job)
{
    free(job->stack);
    free(job);
}

static void EmptyGarbageBin(void)
{
    struct usched_job* i;

    mtx_lock(&g_scheduler.lock);
    i = g_scheduler.garbage_bin;
    while (i) {
        struct usched_job* next = i->next;
        TaskDestroy(i);
        i = next;
    }
    g_scheduler.garbage_bin = NULL;
    mtx_unlock(&g_scheduler.lock);
}

void usched_yield(void)
{
    struct usched_job* current;
    struct usched_job* next;

    if (!g_scheduler.current) {
        // if no active thread and no ready threads then we can safely just return
        if (!g_scheduler.ready) {
            return;
        }

        // we are running in scheduler context, make sure we store
        // this context, so we can return to here when we run out of tasks
        // to execute
        if (setjmp(g_scheduler.context)) {
            EmptyGarbageBin();
            return;
        }
    }

    current = g_scheduler.current;

    mtx_lock(&g_scheduler.lock);
    if (current) {
        if (SHOULD_RESCHEDULE(current)) {
            AppendJob(&g_scheduler.ready, current);
        }
        else if (current->state == JobState_FINISHING) {
            AppendJob(&g_scheduler.garbage_bin, current);
        }
    }
    next = GetNextReady(&g_scheduler);
    mtx_unlock(&g_scheduler.lock);

    g_scheduler.current = next;

    // if we run out of tasks to execute we want to return exit the scheduler
    if (!g_scheduler.current) {
        longjmp(g_scheduler.context, 1);
    }

    // Should always be the last call
    SwitchTask(current, next);
}

void* usched_task_queue(usched_task_fn entry, void* argument)
{
    struct usched_job* job;

    job = malloc(sizeof(struct usched_job));
    if (!job) {
        errno = ENOMEM;
        return NULL;
    }

    job->stack = malloc(4096 * 4);
    if (!job->stack) {
        free(job);
        errno = ENOMEM;
        return NULL;
    }

    job->stack_size = 4096 * 4;
    job->state = JobState_CREATED;
    job->next = NULL;
    job->entry = entry;
    job->argument = argument;
    job->cancelled = 0;
    AppendJob(&g_scheduler.ready, job);

    return job;
}

void usched_task_cancel(void* cancellationToken)
{
    if (!cancellationToken) {
        return;
    }

    ((struct usched_job*)cancellationToken)->cancelled = 1;
}

int usched_ct_is_cancelled(void* cancellationToken)
{
    if (!cancellationToken) {
        return 0;
    }

    return ((struct usched_job*)cancellationToken)->cancelled;
}

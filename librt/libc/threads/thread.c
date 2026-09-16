/**
 * Copyright 2026, Philip Meulengracht
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
 */

#include <errno.h>
#include <os/usched/job.h>
#include <stdlib.h>
#include <threads.h>

struct thrd_start_context {
    thrd_start_t func;
    void*        arg;
};

static void __thrd_start(void* argument, void* cancellationToken)
{
    struct thrd_start_context* context = argument;
    thrd_start_t               func = context->func;
    void*                      arg = context->arg;
    int                        result;

    (void)cancellationToken;

    free(context);
    result = func(arg);
    usched_job_exit(result);
}

int
thrd_create(
    _In_ thrd_t*      thr,
    _In_ thrd_start_t func,
    _In_ void*        arg)
{
    struct thrd_start_context* context;

    context = malloc(sizeof(struct thrd_start_context));
    if (context == NULL) {
        errno = ENOMEM;
        return thrd_nomem;
    }

    context->func = func;
    context->arg = arg;

    *thr = usched_job_queue(__thrd_start, context);
    if (*thr == UUID_INVALID) {
        free(context);
        return __to_thrd_error(-1);
    }
    return thrd_success;
}

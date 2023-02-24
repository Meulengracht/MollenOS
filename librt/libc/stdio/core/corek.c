/**
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
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "internal/_file.h"

#include "assert.h"
#include "spinlock.h"
#include "threading.h"
#include "stdio.h"

static Spinlock_t g_printLock = OS_SPINLOCK_INIT;
static FILE g_stdout = { 0 };
static FILE g_stdin  = { 0 };
static FILE g_stderr = { 0 };

void usched_mtx_init(struct usched_mtx* mutex, int type) {
    _CRT_UNUSED(mutex);
    _CRT_UNUSED(type);
}

void flockfile(FILE* stream) {
    assert(stream != NULL);
    SpinlockAcquire(&g_printLock);
}

void funlockfile(FILE* stream) {
    assert(stream != NULL);
    SpinlockRelease(&g_printLock);
}

FILE*
__get_std_handle(
    _In_ int n)
{
    switch (n) {
        case STDOUT_FILENO: {
            return &g_stdout;
        }
        case STDIN_FILENO: {
            return &g_stdin;
        }
        case STDERR_FILENO: {
            return &g_stderr;
        }
        default: {
            return NULL;
        }
    }
}

int wctomb(char *mbchar, wchar_t wchar) {
    _CRT_UNUSED(mbchar);
    _CRT_UNUSED(wchar);
    return 0;
}

uuid_t ThreadsCurrentId(void) {
    return ThreadCurrentHandle();
}

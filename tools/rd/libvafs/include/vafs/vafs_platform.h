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
 * Platform specific bits
 * - Contains macros and includes for specific platforms
 */

#ifndef __PLATFORM_H__
#define __PLATFORM_H__

#ifndef MIN
#define MIN(a,b) (((a)<(b))?(a):(b))
#endif

#ifndef MAX
#define MAX(a,b) (((a)>(b))?(a):(b))
#endif

#if defined(_WIN32) || defined(_WIN64)
#include <windows.h>

typedef CRITICAL_SECTION mtx_t;
#define thrd_success 0
#define thrd_error   -1

#define mtx_plain NULL

static inline int mtx_init(mtx_t* mtx, void* unused) {
    InitializeCriticalSection(mtx);
    return thrd_success;
}

#define mtx_destroy DeleteCriticalSection

static inline int mtx_trylock(mtx_t* mtx) {
    BOOL status = TryEnterCriticalSection(mtx);
    return status == TRUE ? thrd_success : thrd_error;
}

static inline int mtx_lock(mtx_t* mtx) {
    EnterCriticalSection(mtx);
    return thrd_success;
}

static inline int mtx_unlock(mtx_t* mtx) {
    LeaveCriticalSection(mtx);
    return thrd_success;
}

#else
#include <threads.h>
#endif

#ifdef _MSC_VER
#define VAFS_STRUCT(name, body) __pragma(pack(push, 1)) typedef struct _##name body name##_t __pragma(pack(pop))
#else
#define VAFS_STRUCT(name, body) typedef struct __attribute__((packed)) _##name body name##_t
#endif

#endif //!__PLATFORM_H__

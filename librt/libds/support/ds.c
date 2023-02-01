/**
 * Copyright 2011, Philip Meulengracht
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
 *
 * MollenOS - Generic Data Structures (Shared)
 */

#define __TRACE

#include <ds/ds.h>

#if defined(VALI)
#ifdef __LIBDS_KERNEL_BUILD
#include <arch/interrupts.h>
#include <memoryspace.h>
#include <machine.h>
#include <stdio.h>
#include <debug.h>
#include <heap.h>

extern oserr_t ScFutexWait(OSAsyncContext_t*, OSFutexParameters_t*);
extern oserr_t ScFutexWake(OSFutexParameters_t*);
#else
#include <os/futex.h>
#include <ddk/utils.h>
#include <stdlib.h>
#include <stdio.h>
#endif

#else
// Host build
#include <stdio.h>
#include <stdlib.h>
#include <stdatomic.h>
#include <stdarg.h>
extern void Syscall_FutexWait(OSFutexParameters_t*);
extern void Syscall_FutexWake(OSFutexParameters_t*);
extern oserr_t OSFutex(OSFutexParameters_t* parameters, OSAsyncContext_t* asyncContext);

#define TRACE(...)   printf("%s\n", __VA_ARGS__)
#define WARNING(...) printf("%s\n", __VA_ARGS__)
#define ERROR(...)   fprintf(stderr, "%s\n", __VA_ARGS__)
#endif


/*******************************************************************************
 * Support Methods (DS)
 *******************************************************************************/
void* dsalloc(size_t size)
{
#ifdef __LIBDS_KERNEL_BUILD
	return kmalloc(size);
#else
	return malloc(size);
#endif
}

void dsfree(void* pointer)
{
#ifdef __LIBDS_KERNEL_BUILD
	kfree(pointer);
#else
	free(pointer);
#endif
}

void dswait(OSFutexParameters_t* params, OSAsyncContext_t* asyncContext)
{
#ifdef __LIBDS_KERNEL_BUILD
    ScFutexWait(asyncContext, params);
#else
    OSFutex(params, asyncContext);
#endif
}

void dswake(OSFutexParameters_t* params)
{
#ifdef __LIBDS_KERNEL_BUILD
    ScFutexWake(params);
#else
    OSFutex(params, NULL);
#endif
}

void dstrace(const char* fmt, ...)
{
    char    buffer[256] = { 0 };
    va_list args;

    va_start(args, fmt);
    vsnprintf(&buffer[0], sizeof(buffer), fmt, args);
    va_end(args);
    TRACE(&buffer[0]);
}

void dswarning(const char* fmt, ...)
{
    char    buffer[256] = {0 };
    va_list args;

    va_start(args, fmt);
    vsnprintf(&buffer[0], sizeof(buffer), fmt, args);
    va_end(args);
    WARNING(&buffer[0]);
}

void dserror(const char* fmt, ...)
{
    char Buffer[256] = { 0 };
    va_list Arguments;

    va_start(Arguments, fmt);
    vsnprintf(&Buffer[0], sizeof(Buffer), fmt, Arguments);
    va_end(Arguments);
    ERROR(&Buffer[0]);
}

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

#include <os/mollenos.h>
#include <ds/ds.h>
#include <string.h>

#define __MODULE "DATA"
#ifdef LIBC_KERNEL
#include <arch/interrupts.h>
#include <memoryspace.h>
#include <machine.h>
#include <stdio.h>
#include <debug.h>
#include <heap.h>

extern OsStatus_t ScFutexWait(FutexParameters_t*);
extern OsStatus_t ScFutexWake(FutexParameters_t*);
#else
#include <internal/_syscalls.h>
#include <ddk/utils.h>
#include <stdlib.h>
#include <stdio.h>
#endif

/*******************************************************************************
 * Support Methods (DS)
 *******************************************************************************/
void* dsalloc(size_t size)
{
#ifdef LIBC_KERNEL
	return kmalloc(size);
#else
	return malloc(size);
#endif
}

void dsfree(void* pointer)
{
#ifdef LIBC_KERNEL
	kfree(pointer);
#else
	free(pointer);
#endif
}

void dslock(SafeMemoryLock_t* lock)
{
    int locked = 1;

#ifdef LIBC_KERNEL
    IntStatus_t flags = InterruptDisable();
#endif
    while (1) {
        int val = atomic_exchange(&lock->SyncObject, locked);
        if (!val) {
            break;
        }
    }
#ifdef LIBC_KERNEL
    lock->Flags = flags;
#endif
}

void dsunlock(SafeMemoryLock_t* lock)
{
#ifdef LIBC_KERNEL
    IntStatus_t flags = lock->Flags;
#endif
    atomic_store(&lock->SyncObject, 0);
#ifdef LIBC_KERNEL
    InterruptRestoreState(flags);
#endif
}

void dswait(FutexParameters_t* params)
{
#ifdef LIBC_KERNEL
    ScFutexWait(params);
#else
    Syscall_FutexWait(params);
#endif
}

void dswake(FutexParameters_t* params)
{
#ifdef LIBC_KERNEL
    ScFutexWake(params);
#else
    Syscall_FutexWake(params);
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

int dsmatchkey(KeyType_t type, DataKey_t key1, DataKey_t key2)
{
	switch (type) {
        case KeyId: {
			if (key1.Value.Id == key2.Value.Id) {
                return 0;
            }
        } break;
		case KeyInteger: {
			if (key1.Value.Integer == key2.Value.Integer) {
                return 0;
            }
		} break;
		case KeyString: {
			return strcmp(key1.Value.String.Pointer, key2.Value.String.Pointer);
		} break;
	}
	return -1;
}

int dssortkey(KeyType_t type, DataKey_t key1, DataKey_t key2)
{
	switch (type) {
        case KeyId: {
			if (key1.Value.Id == key2.Value.Id)
				return 0;
			else if (key1.Value.Id > key2.Value.Id)
				return 1;
			else
				return -1;
        } break;
		case KeyInteger: {
			if (key1.Value.Integer == key2.Value.Integer)
				return 0;
			else if (key1.Value.Integer > key2.Value.Integer)
				return 1;
			else
				return -1;
		} break;
		case KeyString: {
			return strcmp(key1.Value.String.Pointer, key2.Value.String.Pointer);
		} break;
	}
	return 0;
}

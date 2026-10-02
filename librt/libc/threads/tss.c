/**
 * MollenOS
 *
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
//#define __TRACE

#include <ds/hashtable.h>
#include <ddk/utils.h>
#include <internal/_utils.h>
#include <internal/_tls.h>
#include <os/spinlock.h>
#include <string.h>
#include <stdio.h>
#include <threads.h>
#include "tss.h"

#define TSS_MAX_KEYS 64

struct tss_thread_scope {
    uintptr_t  storage;
    void*      value;
};

struct tss_object {
    int        allocated;
    tss_dtor_t  destructor;
    hashtable_t values; // hashtable of tss_thread_scope
};

struct tss_process_scope {
    struct tss_object tss[TSS_MAX_KEYS];
};

// hashtable functions for tss_object::values
static uint64_t tss_object_hash(const void* element);
static int      tss_object_cmp(const void* element1, const void* element2);

static spinlock_t               g_tssLock = _SPN_INITIALIZER_NP;
static struct tss_process_scope g_tss     = {
        { 0 },
};

static int __initialize_tss_object(struct tss_object* tss, tss_dtor_t destructor)
{
    int status;

    status = hashtable_construct(
        &tss->values, 0,
        sizeof(struct tss_thread_scope),
        tss_object_hash,
        tss_object_cmp
    );
    if (status) {
        return status;
    }
    
    tss->destructor = destructor;
    tss->allocated = 1;
    return 0;
}

static void __destroy_tss_object(struct tss_object* tss)
{
    hashtable_destroy(&tss->values);
    tss->destructor = NULL;
    tss->allocated = 0;
}

int
tss_create(
    _In_ tss_t*     tssKey,
    _In_ tss_dtor_t destructor)
{
    tss_t result = TSS_KEY_INVALID;
    int   i;

    spinlock_acquire(&g_tssLock);
    for (i = 0; i < TSS_MAX_KEYS; i++) {
        if (!g_tss.tss[i].allocated) {
            int status = __initialize_tss_object(&g_tss.tss[i], destructor);
            if (status) {
                break;
            }
            result = (tss_t)i;
            break;
        }
    }
    spinlock_release(&g_tssLock);

    if (result != TSS_KEY_INVALID) *tssKey = result;
    else                            return thrd_error;
    return thrd_success;
}

void
tss_delete(
    _In_ tss_t tssID)
{
    if (tssID >= TSS_MAX_KEYS) {
        return;
    }

    spinlock_acquire(&g_tssLock);
    if (g_tss.tss[tssID].allocated) {
        __destroy_tss_object(&g_tss.tss[tssID]);
    }
    spinlock_release(&g_tssLock);
}

/* tss_get
 * Returns the value held in thread-specific storage for the current thread 
 * identified by tss_key. Different threads may get different values identified by the same key. */
void*
tss_get(
    _In_ tss_t tssKey)
{
    struct tss_thread_scope* entry;
    void*                    result = NULL;

    if (tssKey >= TSS_MAX_KEYS) {
        return NULL;
    }

    spinlock_acquire(&g_tssLock);
    if (!g_tss.tss[tssKey].allocated) {
        spinlock_release(&g_tssLock);
        return NULL;
    }
    
    entry = hashtable_get(
        &g_tss.tss[tssKey].values,
        &(struct tss_thread_scope) { 
            .storage = (uintptr_t)__tls_current() 
        }
    );
    if (entry != NULL) {
        result = entry->value;
    }
    spinlock_release(&g_tssLock);
    return result;
}

/* tss_set
 * Sets the value of the thread-specific storage identified by tss_id for the 
 * current thread to val. Different threads may set different values to the same key. */
int
tss_set(
    _In_ tss_t tssKey,
    _In_ void* val)
{
    struct tss_thread_scope* stored;
    int                      status;

    if (tssKey >= TSS_MAX_KEYS) {
        errno = EINVAL;
        return thrd_error;
    }

    struct tss_thread_scope key = { 
        .storage = (uintptr_t)__tls_current(),
        .value = val
    };

    spinlock_acquire(&g_tssLock);
    if (!g_tss.tss[tssKey].allocated) {
        spinlock_release(&g_tssLock);
        return thrd_error;
    }
    
    if (!val) {
        hashtable_remove(&g_tss.tss[tssKey].values, &key);
        spinlock_release(&g_tssLock);
        return thrd_success;
    }

    // Catch any insertion issue
    errno = EOK;
    hashtable_set(&g_tss.tss[tssKey].values, &key);
    if (errno == ENOMEM) {
        status = thrd_nomem;
    } else {
        status = thrd_success;
    }

    spinlock_release(&g_tssLock);
    return status;
}

/* Destructors execute in the terminating logical thread. Clear each value
 * before invoking its destructor, with no lock or hashtable pointer retained
 * across callbacks. Keys and other threads' values survive this cleanup. */
void tss_cleanup()
{
    // Use storage pointer: job IDs are recycled and share a numeric 
    // type with kernel-thread IDs.
    struct tss_thread_scope key = {
        .storage = (uintptr_t)__tls_current()
    };
    
    // Sanitize that we have a TLS tlb
    if (!key.storage) {
        return;
    }
    
    // Iterate over TSS destructor iterations to clean up thread-specific storage.
    // And do this for up to TSS_DTOR_ITERATIONS passes.
    for (int pass = 0; pass < TSS_DTOR_ITERATIONS; ++pass) {
        int invoked = 0;
        
        for (int i = 0; i < TSS_MAX_KEYS; ++i) {
            tss_dtor_t destructor = NULL;
            void*      value = NULL;
            
            spinlock_acquire(&g_tssLock);
            if (g_tss.tss[i].allocated) {
                struct tss_thread_scope* entry = hashtable_get(&g_tss.tss[i].values, &key);
                if (entry) {
                    value = entry->value;
                    destructor = g_tss.tss[i].destructor;
                    hashtable_remove(&g_tss.tss[i].values, &key);
                }
            }
            spinlock_release(&g_tssLock);
            
            if (value && destructor) {
                invoked = 1;
                destructor(value);
            }
        }
        
        if (!invoked) {
            break;
        }
    }
    
    // Cleanup any remaining TSS values for this thread
    spinlock_acquire(&g_tssLock);
    for (int i = 0; i < TSS_MAX_KEYS; ++i) {
        if (!g_tss.tss[i].allocated) {
            continue;
        }
        hashtable_remove(&g_tss.tss[i].values, &key);
    }
    spinlock_release(&g_tssLock);
}

static uint64_t tss_object_hash(const void* element) {
    const struct tss_thread_scope* tss = element;
    return (uint64_t)tss->storage;
}

static int tss_object_cmp(const void* element1, const void* element2) {
    const struct tss_thread_scope* tss1 = element1;
    const struct tss_thread_scope* tss2 = element2;
    return tss1->storage == tss2->storage ? 0 : -1;
}

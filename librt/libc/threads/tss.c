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
#include <os/spinlock.h>
#include <string.h>
#include <stdio.h>
#include <threads.h>
#include "tss.h"

#define TSS_MAX_KEYS 64

struct tss_thread_scope {
    thrd_t     thread_id;
    void*      value;
    tss_dtor_t destructor;
};

struct tss_object {
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
    tss->destructor = destructor;
    return hashtable_construct(
            &tss->values, 0,
            sizeof(struct tss_thread_scope),
            tss_object_hash,
            tss_object_cmp
    );
}

static void __destroy_tss_object(struct tss_object* tss)
{
    hashtable_destroy(&tss->values);
    tss->destructor = NULL;
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
        if (g_tss.tss[i].destructor == NULL) {
            __initialize_tss_object(&g_tss.tss[i], destructor);
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
    __destroy_tss_object(&g_tss.tss[tssID]);
    spinlock_release(&g_tssLock);
}

/* tss_get
 * Returns the value held in thread-specific storage for the current thread 
 * identified by tss_key. Different threads may get different values identified by the same key. */
void*
tss_get(
    _In_ tss_t tssKey)
{
    void* result = NULL;

    if (tssKey >= TSS_MAX_KEYS) {
        return NULL;
    }

    spinlock_acquire(&g_tssLock);
    struct tss_thread_scope* entry = hashtable_get(&g_tss.tss[tssKey].values,
            &(struct tss_thread_scope) { .thread_id = __crt_thread_id() });
    if (entry != NULL) {
        result = entry;
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
    if (tssKey >= TSS_MAX_KEYS) {
        errno = EINVAL;
        return thrd_error;
    }

    spinlock_acquire(&g_tssLock);
    hashtable_set(&g_tss.tss[tssKey].values,
                  &(struct tss_thread_scope) {
        .thread_id = __crt_thread_id(),
        .value     = val,
        .destructor = g_tss.tss[tssKey].destructor
    });
    spinlock_release(&g_tssLock);
    return thrd_success;
}

static inline int __execute_tss_entry(struct tss_thread_scope* tss) {
    if (tss->value != NULL && tss->destructor != NULL) {
        void* originalValue = tss->value;
        tss->value = NULL;
        tss->destructor(originalValue);
        if (tss->value != NULL) {
            return 1; // we need to run this again
        }
    }
    return 0;
}

static void
tss_object_enum(
        _In_ int         index,
        _In_ const void* element,
        _In_ void*       userContext)
{
    struct tss_thread_scope* tss              = (struct tss_thread_scope*)element;
    int*                     valuesRemaining  = (int*)userContext;
    _CRT_UNUSED(index);
    TRACE("tss_object_enum()");
    *valuesRemaining = __execute_tss_entry(tss);
}

/* tss_cleanup
 * Cleans up tls storage for the given thread id and invokes all registered
 * at-exit handlers for the thread. Invoking this with UUID_INVALID invokes
 * process at-exit handlers. */
void
tss_cleanup(
        _In_ thrd_t threadID)
{
    int passesRemaining = TSS_DTOR_ITERATIONS;
    int valuesRemaining;
    TRACE("tss_cleanup(%u, 0x%x, %i)", threadID, dsoHandle, exitCode);

    // Execute all stored destructors untill there is no
    // more values left or we reach the maximum number of passes
    do {
        valuesRemaining = 0;
        for (int i = 0; i < TSS_MAX_KEYS; i++) {
            if (g_tss.tss[i].destructor != NULL) {
                // Either we are exucuting by thread, or we are executing by
                // all threads. If we are executing by thread, we simply try
                // to look up the thread entry
                if (threadID != UUID_INVALID) {
                    struct tss_thread_scope* tss = hashtable_get(
                            &g_tss.tss[i].values,
                            &(struct tss_thread_scope) { .thread_id = threadID }
                    );
                    if (tss) {
                        valuesRemaining += __execute_tss_entry(tss);
                    }
                } else {
                    // Run for all threads, so we invoke the callback
                    hashtable_enumerate(
                            &g_tss.tss[i].values,
                            tss_object_enum,
                            &valuesRemaining
                    );
                }
            }
        }
        passesRemaining--;
    } while (valuesRemaining != 0 && passesRemaining);

    // Cleanup all stored tls-keys by this thread
    spinlock_acquire(&g_tssLock);
    for (int i = 0; i < TSS_MAX_KEYS; i++) {
        if (g_tss.tss[i].destructor != NULL) {
            __destroy_tss_object(&g_tss.tss[i]);
        }
    }
    spinlock_release(&g_tssLock);
}

static uint64_t tss_object_hash(const void* element) {
    const struct tss_thread_scope* tss = element;
    return (uint64_t)tss->thread_id;
}

static int tss_object_cmp(const void* element1, const void* element2) {
    const struct tss_thread_scope* tss1 = element1;
    const struct tss_thread_scope* tss2 = element2;
    return tss1->thread_id == tss2->thread_id ? 0 : -1;
}

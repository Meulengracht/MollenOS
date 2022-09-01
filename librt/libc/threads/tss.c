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
 *
 *
 * Threading Support Definitions & Structures
 * - This header describes the base threading-structures, prototypes
 *   and functionality, refer to the individual things for descriptions
 * 
 * Process Flow:
 *  - Startup:          __crt_tls_create(main_thread)
 *  - Cleanup (Normal)  __cxa_exithandlers, tss_cleanup(thread/process), tls_destroy
 *  - Cleanup (Quick)   __cxa_exithandlers, tss_cleanup_quick(thread/process), tls_destroy
 * 
 * Thread Flow:
 *  - Startup:          __crt_tls_create(new_thread), __cxa_threadinitialize
 *  - Cleanup:          tss_cleanup(thread), tls_destroy, __cxa_threadfinalize
 */
//#define __TRACE

#include <os/spinlock.h>
#include <ds/list.h>
#include <ds/hashtable.h>
#include <ddk/utils.h>
#include <threads.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "tss.h"

#define TSS_MAX_KEYS			    64
#define TSS_ATEXIT_CXA              1
#define TSS_ATEXIT_THREAD_CXA       2

struct tss_thread_scope {
    thrd_t     thread_id;
    void*      value;
    tss_dtor_t destructor;
};

typedef struct TlsAtExit {
    int              Type;
    void*            DsoHandle;
    void*            Argument;
    union {
        void       (*Function)(void*, int);
        tss_dtor_t Destructor;
    } AtExit;
} TlsAtExit_t;

struct tss_object {
    tss_dtor_t  destructor;
    hashtable_t values; // hashtable of tss_thread_scope
};

struct tss_process_scope {
    struct tss_object tss[TSS_MAX_KEYS];

    // thread specific at-exits
    hashtable_t*      at_exit_thread;
    hashtable_t*      at_quick_exit_thread;

    // global at-exits
    hashtable_t*      at_exit;
    hashtable_t*      at_quick_exit;
    int               TlsAtExitHasRun;
};

// hashtable functions for tss_object::values
static uint64_t tss_object_hash(const void* element);
static int      tss_object_cmp(const void* element1, const void* element2);

static spinlock_t               g_tssLock = _SPN_INITIALIZER_NP(spinlock_plain);
static struct tss_process_scope g_tss     = {
        { 0 },
        NULL, NULL,
        NULL, NULL,
        0
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
            &(struct tss_thread_scope) { .thread_id = thrd_current() });
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
        .thread_id = thrd_current(),
        .value     = val,
        .destructor = g_tss.tss[tssKey].destructor
    });
    spinlock_release(&g_tssLock);
    return thrd_success;
}

static void __register_atexit(
    _In_ Collection_t*  atExitList,
    _In_ thrd_t         threadId,
    _In_ void           (*function)(void*),
    _In_ void*          argument,
    _In_ void*          dsoHandle)
{
    TlsAtExit_t* atExitFn;
    
    TRACE("__register_atexit(%u, 0x%x)", threadId, dsoHandle);
    if (g_tss.TlsAtExitHasRun != 0) {
        return;
    }

    atExitFn = (TlsAtExit_t*)malloc(sizeof(TlsAtExit_t));
    if (atExitFn == NULL) {
        return;
    }
    memset(atExitFn, 0, sizeof(TlsAtExit_t));

    atExitFn->ListHeader.Key.Value.Id = threadId;
    atExitFn->Argument                = argument;
    atExitFn->DsoHandle               = dsoHandle;
    if (threadId == UUID_INVALID) {
        atExitFn->Type            = TSS_ATEXIT_CXA;
        atExitFn->AtExit.Function = (void(*)(void*, int))function;
    }
    else {
        atExitFn->Type              = TSS_ATEXIT_THREAD_CXA;
        atExitFn->AtExit.Destructor = function;
    }
    CollectionAppend(atExitList, &atExitFn->ListHeader);
}

void
tss_atexit(
        _In_ thrd_t threadID,
        _In_ void   (*atExitFn)(void*),
        _In_ void*  argument,
        _In_ void*  dsoHandle)
{
    TRACE("tss_atexit(%u, 0x%x)", threadID, dsoHandle);
    __register_atexit(&g_tss.TlsAtExit, threadID, atExitFn, argument, dsoHandle);
}

void
tss_atexit_quick(
        _In_ thrd_t threadID,
        _In_ void   (*atExitFn)(void*),
        _In_ void*  argument,
        _In_ void*  dsoHandle)
{
    TRACE("tss_atexit_quick(%u, 0x%x)", threadID, dsoHandle);
    __register_atexit(&g_tss.TlsAtQuickExit, threadID, atExitFn, argument, dsoHandle);
}

static void
__callatexit(
        _In_ Collection_t* List,
        _In_ thrd_t        threadID,
        _In_ void*         DsoHandle,
        _In_ int           ExitCode)
{
    CollectionItem_t*   Node;
    DataKey_t           Key = { .Value.Id = threadID };
    int                 Skip = 0;

    TRACE("__callatexit(%u, 0x%x, %i)", threadID, DsoHandle, ExitCode);
    if (g_tss.TlsAtExitHasRun != 0) {
        return;
    }

    // To avoid recursive at exit calls due to shutdown failure, register that we have
    // now run exit for primary application.
    if (threadID == UUID_INVALID && DsoHandle == NULL) {
        g_tss.TlsAtExitHasRun = 1;
    }

    Node = CollectionGetNodeByKey(List, Key, Skip);
    while (Node != NULL) {
        TlsAtExit_t* Function = (TlsAtExit_t*)Node;
        if (Function->DsoHandle == DsoHandle || DsoHandle == NULL) {
            if (Function->Type == TSS_ATEXIT_CXA) {
                Function->AtExit.Function(Function->Argument, ExitCode);
            }
            else if (Function->Type == TSS_ATEXIT_THREAD_CXA) {
                Function->AtExit.Destructor(Function->Argument);
            }
            CollectionRemoveByNode(List, Node);
            free(Function);
        }
        else {
            Skip++;
        }
        Node = CollectionGetNodeByKey(List, Key, Skip);
    }
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
        _In_ thrd_t threadID,
        _In_ void*  dsoHandle,
        _In_ int    exitCode)
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
    __callatexit(&g_tss.TlsAtExit, threadID, dsoHandle, exitCode);
}

/* tss_cleanup_quick
 * Does not perform any cleanup on tls storage, invokes all registered quick
 * handles instead for the calling thread. Invoking this with UUID_INVALID invokes
 * process at-exit handlers. */
void
tss_cleanup_quick(_In_ thrd_t thr, _In_ void* DsoHandle, _In_ int ExitCode)
{
    TRACE("tss_cleanup_quick(%u, 0x%x, %i)", thr, DsoHandle, ExitCode);
    __callatexit(&g_tss.TlsAtQuickExit, thr, DsoHandle, ExitCode);
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

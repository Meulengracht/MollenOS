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
 *  - Cleanup (Normal)  __cxa_exithandlers, tls_cleanup(thread/process), tls_destroy
 *  - Cleanup (Quick)   __cxa_exithandlers, tls_cleanup_quick(thread/process), tls_destroy
 * 
 * Thread Flow:
 *  - Startup:          __crt_tls_create(new_thread), __cxa_threadinitialize
 *  - Cleanup:          tls_cleanup(thread), tls_destroy, __cxa_threadfinalize
 */
//#define __TRACE

#include <os/spinlock.h>
#include <ds/collection.h>
#include <ddk/utils.h>
#include <threads.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include "../../libc/locale/setlocale.h"
#include "tls.h"
#include "internal/_utils.h"

#define TLS_MAX_KEYS			    64
#define TLS_ATEXIT_CXA              1
#define TLS_ATEXIT_THREAD_CXA       2

typedef struct TlsThreadInstance {
    CollectionItem_t ListHeader;
    tss_t            Key;
    void*            Value;
    tss_dtor_t       Destructor;
} TlsThreadInstance_t;

typedef struct TlsAtExit {
    CollectionItem_t ListHeader;
    int              Type;
    void*            DsoHandle;
    void*            Argument;
    union {
        void       (*Function)(void*, int);
        tss_dtor_t Destructor;
    } AtExit;
} TlsAtExit_t;

typedef struct TlsProcessInstance {
    int          Keys[TLS_MAX_KEYS];
    tss_dtor_t   Dss[TLS_MAX_KEYS];
    Collection_t Tls;                // List of TlsThreadInstance
    Collection_t TlsAtExit;          // List of TlsAtExit
    Collection_t TlsAtQuickExit;     // List of TlsAtExit
    int          TlsAtExitHasRun;
} TlsProcessInstance_t;

static spinlock_t           g_tlsLock = _SPN_INITIALIZER_NP(spinlock_plain);
static TlsProcessInstance_t g_tls     = { {0 }, { 0 },
                                         COLLECTION_INIT(KeyId),
                                         COLLECTION_INIT(KeyId),
                                         COLLECTION_INIT(KeyId),
                                         0
};
static const char* g_nullEnvironment[] = {
        NULL
};

static const char* const* __clone_env_block(void)
{
    const char* const* source = __crt_environment();
    char**             copy;
    int                count;

    if (source == NULL) {
        return (const char* const*)g_nullEnvironment;
    }

    count = 0;
    while (source[count]) {
        count++;
    }

    copy = calloc(count + 1, sizeof(char*));
    if (copy == NULL) {
        return (const char* const*)g_nullEnvironment;
    }

    count = 0;
    while (source[count]) {
        copy[count] = strdup(source[count]);
        count++;
    }
    return (const char* const*)copy;
}

oscode_t
__crt_tls_create(
    _In_ thread_storage_t* tls)
{
    struct dma_buffer_info info;
    void*                  buffer;
    
    memset(tls, 0, sizeof(thread_storage_t));

    // Store it at reserved pointer place first
    __set_reserved(0, (size_t)tls);
    __set_reserved(1, (size_t)&tls->tls_array[0]);
    __set_reserved(11, (size_t)&tls->tls_array[0]);
    
    // Initialize members to default values
    tls->thr_id = UUID_INVALID;
    tls->err_no = EOK;
    tls->locale = __get_global_locale();
    tls->seed   = 1;

    // this may end up returning NULL environment for the primary thread if the
    // CRT hasn't fully initialized yet. Ignore it, and see what happens
    tls->env_block = __clone_env_block();

    // Setup a local transfer buffer for stdio operations
    // TODO: do on first read/write instead?
    buffer = malloc(BUFSIZ);
    if (buffer == NULL) {
        return OsOutOfMemory;
    }

    info.name     = "thread_tls";
    info.length   = BUFSIZ;
    info.capacity = BUFSIZ;
    info.flags    = DMA_PERSISTANT;
    info.type     = DMA_TYPE_DRIVER_32;
    return dma_export(buffer, &info, &tls->transfer_buffer);
}

static void __destroy_env_block(char** env)
{
    for (int i = 0; env[i] != NULL; i++) {
        free(env[i]);
    }
    free(env);
}

oscode_t
tls_destroy(
    _In_ thread_storage_t* tls)
{
    // TODO: this is called twice for primary thread. Look into this
    if (tls->transfer_buffer.buffer != NULL) {
        dma_detach(&tls->transfer_buffer);
        free(tls->transfer_buffer.buffer);
        tls->transfer_buffer.buffer = NULL;
    }
    if (tls->env_block != NULL && tls->env_block != g_nullEnvironment) {
        __destroy_env_block((char**)tls->env_block);
        tls->env_block = NULL;
    }
    return OsOK;
}

thread_storage_t*
tls_current(void)
{
    return (thread_storage_t*)__get_reserved(0);
}

int
tss_create(
    _In_ tss_t*     tss_key,
    _In_ tss_dtor_t destructor)
{
    tss_t result = TSS_KEY_INVALID;
    int   i;

    spinlock_acquire(&g_tlsLock);
    for (i = 0; i < TLS_MAX_KEYS; i++) {
        if (g_tls.Keys[i] == 0) {
            g_tls.Keys[i] = 1;
            g_tls.Dss[i]  = destructor;
            result = (tss_t)i;
            break;
        }
    }
    spinlock_release(&g_tlsLock);

    if (result != TSS_KEY_INVALID) *tss_key = result;
    else                            return thrd_error;
    return thrd_success;
}

/* tss_delete
 * Destroys the thread-specific storage identified by tss_id. */
void
tss_delete(
    _In_ tss_t tss_id)
{
    CollectionItem_t* Node;
    if (tss_id >= TLS_MAX_KEYS) {
        return;
    }

    // Iterate nodes without auto-linking, we do that manually
    spinlock_acquire(&g_tlsLock);
    _foreach_nolink(Node, &g_tls.Tls) {
        TlsThreadInstance_t *Tls = (TlsThreadInstance_t*)Node;

        // Make sure we delete all instances of the key
        // If we find one, we need to unlink it and get it's
        // successor before destroying the node
        if (Tls->Key == tss_id) {
            Node = CollectionUnlinkNode(&g_tls.Tls, Node);
            free(Tls);
        }
        else {
            Node = Node->Link;
        }
    }
    g_tls.Keys[tss_id] = 0;
    g_tls.Dss[tss_id]  = NULL;
    spinlock_release(&g_tlsLock);
}

/* tss_get
 * Returns the value held in thread-specific storage for the current thread 
 * identified by tss_key. Different threads may get different values identified by the same key. */
void*
tss_get(
    _In_ tss_t tss_key)
{
    CollectionItem_t* Node = NULL;
    void *Result            = NULL;
    DataKey_t tKey          = { .Value.Id = thrd_current() };

    if (tss_key >= TLS_MAX_KEYS) {
        return NULL;
    }

    // Iterate the list of TLS instances and 
    // find the one that contains the tls-key
    spinlock_acquire(&g_tlsLock);
    _foreach(Node, &g_tls.Tls) {
        TlsThreadInstance_t* Tls = (TlsThreadInstance_t*)Node;
        if (!dsmatchkey(KeyId, tKey, Node->Key) && Tls->Key == tss_key) {
            Result = Tls->Value;
            break;
        }
    }
    spinlock_release(&g_tlsLock);
    return Result;
}

/* tss_set
 * Sets the value of the thread-specific storage identified by tss_id for the 
 * current thread to val. Different threads may set different values to the same key. */
int
tss_set(
    _In_ tss_t tss_id,
    _In_ void *val)
{
    TlsThreadInstance_t *NewTls = NULL;
    CollectionItem_t *Node      = NULL;
    DataKey_t tKey              = { .Value.Id = thrd_current() };

    // Sanitize key value
    if (tss_id >= TLS_MAX_KEYS) {
        return thrd_error;
    }

    // Iterate and find if it
    // exists, if exists we override
    spinlock_acquire(&g_tlsLock);
    _foreach(Node, &g_tls.Tls) {
        TlsThreadInstance_t *Tls = (TlsThreadInstance_t*)Node;
        if (!dsmatchkey(KeyId, tKey, Node->Key) && Tls->Key == tss_id) {
            Tls->Value = val;
            spinlock_release(&g_tlsLock);
            return thrd_success;
        }
    }
    spinlock_release(&g_tlsLock);

    NewTls = (TlsThreadInstance_t*)malloc(sizeof(TlsThreadInstance_t));
    if (NewTls == NULL) {
        return thrd_nomem;
    }
    
    memset(NewTls, 0, sizeof(TlsThreadInstance_t));
    NewTls->ListHeader.Key  = tKey;
    NewTls->ListHeader.Data = NewTls;
    NewTls->Key             = tss_id;
    NewTls->Value           = val;
    NewTls->Destructor      = g_tls.Dss[tss_id];

    // Last thing is to append it to the tls-list
    spinlock_acquire(&g_tlsLock);
    CollectionAppend(&g_tls.Tls, &NewTls->ListHeader);
    spinlock_release(&g_tlsLock);
    return thrd_success;
}

/* tls_callback
 * Runs all destructors for a given thread */
void
tls_callback(
    _In_ void*  Data, 
    _In_ int    Index, 
    _In_ void*  Context)
{
    TlsThreadInstance_t* Tls         = (TlsThreadInstance_t*)Data;
    int*                 ValuesLeft  = (int*)Context;
    _CRT_UNUSED(Index);
    TRACE("tls_callback()");

    // Determine whether or not we should run the destructor
    // for this tls-key
    if (Tls->Value != NULL && Tls->Destructor != NULL) {
        void *ArgValue  = Tls->Value;
        Tls->Value      = NULL;
        Tls->Destructor(ArgValue);

        // If the value has been updated, we need another pass
        if (Tls->Value != NULL) {
            (*ValuesLeft)++;
        }
    }
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
    if (g_tls.TlsAtExitHasRun != 0) {
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
        atExitFn->Type            = TLS_ATEXIT_CXA;
        atExitFn->AtExit.Function = (void(*)(void*, int))function;
    }
    else {
        atExitFn->Type              = TLS_ATEXIT_THREAD_CXA;
        atExitFn->AtExit.Destructor = function;
    }
    CollectionAppend(atExitList, &atExitFn->ListHeader);
}

void
tls_atexit(_In_ thrd_t thr, _In_ void (*Function)(void*), _In_ void* Argument, _In_ void* DsoHandle)
{
    TRACE("tls_atexit(%u, 0x%x)", thr, DsoHandle);
    __register_atexit(&g_tls.TlsAtExit, thr, Function, Argument, DsoHandle);
}

void
tls_atexit_quick(_In_ thrd_t thr, _In_ void (*Function)(void*), _In_ void* Argument, _In_ void* DsoHandle)
{
    TRACE("tls_atexit_quick(%u, 0x%x)", thr, DsoHandle);
    __register_atexit(&g_tls.TlsAtQuickExit, thr, Function, Argument, DsoHandle);
}

static void __callatexit(_In_ Collection_t* List, _In_ thrd_t ThreadId, _In_ void* DsoHandle, _In_ int ExitCode)
{
    CollectionItem_t*   Node;
    DataKey_t           Key = { .Value.Id = ThreadId };
    int                 Skip = 0;

    TRACE("__callatexit(%u, 0x%x, %i)", ThreadId, DsoHandle, ExitCode);
    if (g_tls.TlsAtExitHasRun != 0) {
        return;
    }

    // To avoid recursive at exit calls due to shutdown failure, register that we have
    // now run exit for primary application.
    if (ThreadId == UUID_INVALID && DsoHandle == NULL) {
        g_tls.TlsAtExitHasRun = 1;
    }

    Node = CollectionGetNodeByKey(List, Key, Skip);
    while (Node != NULL) {
        TlsAtExit_t* Function = (TlsAtExit_t*)Node;
        if (Function->DsoHandle == DsoHandle || DsoHandle == NULL) {
            if (Function->Type == TLS_ATEXIT_CXA) {
                Function->AtExit.Function(Function->Argument, ExitCode);
            }
            else if (Function->Type == TLS_ATEXIT_THREAD_CXA) {
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

/* tls_cleanup
 * Cleans up tls storage for the given thread id and invokes all registered
 * at-exit handlers for the thread. Invoking this with UUID_INVALID invokes
 * process at-exit handlers. */
void
tls_cleanup(_In_ thrd_t thr, _In_ void* DsoHandle, _In_ int ExitCode)
{
    int         NumberOfPassesLeft  = TSS_DTOR_ITERATIONS;
    int         NumberOfValsLeft    = 0;
    DataKey_t   Key                 = { .Value.Id = thr };
    TRACE("tls_cleanup(%u, 0x%x, %i)", thr, DsoHandle, ExitCode);

    // Execute all stored destructors untill there is no
    // more values left or we reach the maximum number of passes
    CollectionExecuteOnKey(&g_tls.Tls, tls_callback, Key, &NumberOfValsLeft);
    while (NumberOfValsLeft != 0 && NumberOfPassesLeft) {
        NumberOfValsLeft = 0;
        CollectionExecuteOnKey(&g_tls.Tls, tls_callback, Key, &NumberOfValsLeft);
        NumberOfPassesLeft--;
    }

    // Cleanup all stored tls-keys by this thread
    spinlock_acquire(&g_tlsLock);
    while (CollectionRemoveByKey(&g_tls.Tls, Key) == OsOK);
    spinlock_release(&g_tlsLock);
    __callatexit(&g_tls.TlsAtExit, thr, DsoHandle, ExitCode);
}

/* tls_cleanup_quick
 * Does not perform any cleanup on tls storage, invokes all registered quick
 * handles instead for the calling thread. Invoking this with UUID_INVALID invokes
 * process at-exit handlers. */
void
tls_cleanup_quick(_In_ thrd_t thr, _In_ void* DsoHandle, _In_ int ExitCode)
{
    TRACE("tls_cleanup_quick(%u, 0x%x, %i)", thr, DsoHandle, ExitCode);
    __callatexit(&g_tls.TlsAtQuickExit, thr, DsoHandle, ExitCode);
}

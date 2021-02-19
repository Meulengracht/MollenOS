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
 * along with this program.If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * Threading Support Definitions & Structures
 * - This header describes the base threading-structures, prototypes
 *   and functionality, refer to the individual things for descriptions
 * 
 * Process Flow:
 *  - Startup:          tls_create(main_thread)
 *  - Cleanup (Normal)  __cxa_exithandlers, tls_cleanup(thread/process), tls_destroy
 *  - Cleanup (Quick)   __cxa_exithandlers, tls_cleanup_quick(thread/process), tls_destroy
 * 
 * Thread Flow:
 *  - Startup:          tls_create(new_thread), __cxa_threadinitialize
 *  - Cleanup:          tls_cleanup(thread), tls_destroy, __cxa_threadfinalize
 */
//#define __TRACE

#include <os/spinlock.h>
#include <ds/report_parser.h>
#include <ddk/utils.h>
#include <threads.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include "../../libc/locale/setlocale.h"
#include "tls.h"

#define TLS_MAX_KEYS			    64
#define TLS_ATEXIT_CXA              1
#define TLS_ATEXIT_THREAD_CXA       2

typedef struct TlsThreadInstance {
    CollectionItem_t    ListHeader;
    tss_t               Key;
    void*               Value;
    tss_dtor_t          Destructor;
} TlsThreadInstance_t;

typedef struct TlsAtExit {
    CollectionItem_t    ListHeader;
    int                 Type;
    void*               DsoHandle;
    void*               Argument;
    union {
        void            (*Function)(void*, int);
        tss_dtor_t      Destructor;
    } AtExit;
} TlsAtExit_t;

typedef struct TlsProcessInstance {
    int             Keys[TLS_MAX_KEYS];
    tss_dtor_t      Dss[TLS_MAX_KEYS];
    Collection_t    Tls;                // List of TlsThreadInstance
    Collection_t    TlsAtExit;          // List of TlsAtExit
    Collection_t    TlsAtQuickExit;     // List of TlsAtExit
    int             TlsAtExitHasRun;
} TlsProcessInstance_t;

static spinlock_t           TlsLock     = _SPN_INITIALIZER_NP(spinlock_plain);
static TlsProcessInstance_t TlsGlobal   = { { 0 }, { 0 }, 
    COLLECTION_INIT(KeyId),
    COLLECTION_INIT(KeyId),
    COLLECTION_INIT(KeyId),
    0
};

OsStatus_t 
tls_create(
    _In_ thread_storage_t* Tls)
{
    struct dma_buffer_info info;
    void* buffer;
    
    memset(Tls, 0, sizeof(thread_storage_t));

    // Store it at reserved pointer place first
    __set_reserved(0, (size_t)Tls);
    __set_reserved(1, (size_t)&Tls->tls_array[0]);
    __set_reserved(11, (size_t)&Tls->tls_array[0]);
    
    // Initialize members to default values
    Tls->thr_id = UUID_INVALID;
    Tls->err_no = EOK;
    Tls->locale = __get_global_locale();
    Tls->seed   = 1;

    // Setup a local transfer buffer for stdio operations
    // TODO: do on first read/write instead?
    buffer = malloc(BUFSIZ);
    
    info.length   = BUFSIZ;
    info.capacity = BUFSIZ;
    info.flags    = DMA_PERSISTANT;
    return dma_export(buffer, &info, &Tls->transfer_buffer);
}

/* tls_destroy
 * Destroys a thread-storage space should be called by thread crt */
OsStatus_t
tls_destroy(
    _In_ thread_storage_t* Tls)
{
    // TODO: this is called twice for primary thread. Look into this
    if (Tls->transfer_buffer.buffer != NULL) {
        dma_detach(&Tls->transfer_buffer);
        free(Tls->transfer_buffer.buffer);
    }
    return OsSuccess;
}

/* tls_current 
 * Retrieves the local storage space for the current thread */
thread_storage_t*
tls_current(void)
{
    return (thread_storage_t*)__get_reserved(0);
}

/* tss_create
 * Creates new thread-specific storage key and stores it in the object pointed to by tss_key. 
 * Although the same key value may be used by different threads, 
 * the values bound to the key by tss_set are maintained on a per-thread 
 * basis and persist for the life of the calling thread. */
int
tss_create(
    _In_ tss_t*     tss_key,
    _In_ tss_dtor_t destructor)
{
    tss_t   Result = TSS_KEY_INVALID;
    int     i;

    spinlock_acquire(&TlsLock);
    for (i = 0; i < TLS_MAX_KEYS; i++) {
        if (TlsGlobal.Keys[i] == 0) {
            TlsGlobal.Keys[i]   = 1;
            TlsGlobal.Dss[i]    = destructor;
            Result              = (tss_t)i;
            break;
        }
    }
    spinlock_release(&TlsLock);

    if (Result != TSS_KEY_INVALID)  *tss_key = Result;
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
    spinlock_acquire(&TlsLock);
    _foreach_nolink(Node, &TlsGlobal.Tls) {
        TlsThreadInstance_t *Tls = (TlsThreadInstance_t*)Node;

        // Make sure we delete all instances of the key
        // If we find one, we need to unlink it and get it's
        // successor before destroying the node
        if (Tls->Key == tss_id) {
            Node = CollectionUnlinkNode(&TlsGlobal.Tls, Node);
            free(Tls);
        }
        else {
            Node = Node->Link;
        }
    }
    TlsGlobal.Keys[tss_id]  = 0;
    TlsGlobal.Dss[tss_id]   = NULL;
    spinlock_release(&TlsLock);
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
    spinlock_acquire(&TlsLock);
    _foreach(Node, &TlsGlobal.Tls) {
        TlsThreadInstance_t* Tls = (TlsThreadInstance_t*)Node;
        if (!dsmatchkey(KeyId, tKey, Node->Key) && Tls->Key == tss_key) {
            Result = Tls->Value;
            break;
        }
    }
    spinlock_release(&TlsLock);
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
    spinlock_acquire(&TlsLock);
    _foreach(Node, &TlsGlobal.Tls) {
        TlsThreadInstance_t *Tls = (TlsThreadInstance_t*)Node;
        if (!dsmatchkey(KeyId, tKey, Node->Key) && Tls->Key == tss_id) {
            Tls->Value = val;
            spinlock_release(&TlsLock);
            return thrd_success;
        }
    }
    spinlock_release(&TlsLock);

    NewTls = (TlsThreadInstance_t*)malloc(sizeof(TlsThreadInstance_t));
    if (NewTls == NULL) {
        return thrd_nomem;
    }
    
    memset(NewTls, 0, sizeof(TlsThreadInstance_t));
    NewTls->ListHeader.Key  = tKey;
    NewTls->ListHeader.Data = NewTls;
    NewTls->Key             = tss_id;
    NewTls->Value           = val;
    NewTls->Destructor      = TlsGlobal.Dss[tss_id];

    // Last thing is to append it to the tls-list
    spinlock_acquire(&TlsLock);
    CollectionAppend(&TlsGlobal.Tls, &NewTls->ListHeader);
    spinlock_release(&TlsLock);
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
    TlsThreadInstance_t*    Tls         = (TlsThreadInstance_t*)Data;
    int*                    ValuesLeft  = (int*)Context;
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

/* tls_register_atexit 
 * Registers a new atquickexit/atexit handler that will be invoked during thread
 * shutdown or process shutdown. If the thread-id is UUID_INVALID, it's registered
 * as process-shutdown. */
void
tls_register_atexit(
    _In_ Collection_t*  List,
    _In_ thrd_t         ThreadId,
    _In_ void           (*Function)(void*),
    _In_ void*          Argument,
    _In_ void*          DsoHandle)
{
    TlsAtExit_t* AtExitFn;
    
    TRACE("tls_register_atexit(%u, 0x%x)", ThreadId, DsoHandle);
    if (TlsGlobal.TlsAtExitHasRun != 0) {
        return;
    }

    AtExitFn = (TlsAtExit_t*)malloc(sizeof(TlsAtExit_t));
    if (AtExitFn == NULL) {
        return;
    }
    memset(AtExitFn, 0, sizeof(TlsAtExit_t));

    AtExitFn->ListHeader.Key.Value.Id   = ThreadId;
    AtExitFn->Argument                  = Argument;
    AtExitFn->DsoHandle                 = DsoHandle;
    if (ThreadId == UUID_INVALID) {
        AtExitFn->Type              = TLS_ATEXIT_CXA;
        AtExitFn->AtExit.Function   = (void(*)(void*, int))Function;
    }
    else {
        AtExitFn->Type              = TLS_ATEXIT_THREAD_CXA;
        AtExitFn->AtExit.Destructor = Function;
    }
    CollectionAppend(&TlsGlobal.TlsAtExit, &AtExitFn->ListHeader);
}

/* tls_atexit
 * Registers a thread-specific at-exit handler. */
void
tls_atexit(_In_ thrd_t thr, _In_ void (*Function)(void*), _In_ void* Argument, _In_ void* DsoHandle)
{
    TRACE("tls_atexit(%u, 0x%x)", thr, DsoHandle);
    tls_register_atexit(&TlsGlobal.TlsAtExit, thr, Function, Argument, DsoHandle);
}

/* tls_atexit_quick
 * Registers a thread-specific at-exit handler. */
void
tls_atexit_quick(_In_ thrd_t thr, _In_ void (*Function)(void*), _In_ void* Argument, _In_ void* DsoHandle)
{
    TRACE("tls_atexit_quick(%u, 0x%x)", thr, DsoHandle);
    tls_register_atexit(&TlsGlobal.TlsAtQuickExit, thr, Function, Argument, DsoHandle);
}

void
tls_callatexit(_In_ Collection_t* List, _In_ thrd_t ThreadId, _In_ void* DsoHandle, _In_ int ExitCode)
{
    CollectionItem_t*   Node;
    DataKey_t           Key = { .Value.Id = ThreadId };
    int                 Skip = 0;

    TRACE("tls_callatexit(%u, 0x%x, %i)", ThreadId, DsoHandle, ExitCode);
    if (TlsGlobal.TlsAtExitHasRun != 0) {
        return;
    }

    // To avoid recursive at exit calls due to shutdown failure, register that we have
    // now run exit for primary application.
    if (ThreadId == UUID_INVALID && DsoHandle == NULL) {
        TlsGlobal.TlsAtExitHasRun = 1;
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
    CollectionExecuteOnKey(&TlsGlobal.Tls, tls_callback, Key, &NumberOfValsLeft);
    while (NumberOfValsLeft != 0 && NumberOfPassesLeft) {
        NumberOfValsLeft = 0;
        CollectionExecuteOnKey(&TlsGlobal.Tls, tls_callback, Key, &NumberOfValsLeft);
        NumberOfPassesLeft--;
    }

    // Cleanup all stored tls-keys by this thread
    spinlock_acquire(&TlsLock);
    while (CollectionRemoveByKey(&TlsGlobal.Tls, Key) == OsSuccess);
    spinlock_release(&TlsLock);
    tls_callatexit(&TlsGlobal.TlsAtExit, thr, DsoHandle, ExitCode);
}

/* tls_cleanup_quick
 * Does not perform any cleanup on tls storage, invokes all registered quick
 * handles instead for the calling thread. Invoking this with UUID_INVALID invokes
 * process at-exit handlers. */
void
tls_cleanup_quick(_In_ thrd_t thr, _In_ void* DsoHandle, _In_ int ExitCode)
{
    TRACE("tls_cleanup_quick(%u, 0x%x, %i)", thr, DsoHandle, ExitCode);
    tls_callatexit(&TlsGlobal.TlsAtQuickExit, thr, DsoHandle, ExitCode);
}

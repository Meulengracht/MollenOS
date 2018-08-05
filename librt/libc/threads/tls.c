/* MollenOS
 *
 * Copyright 2011 - 2017, Philip Meulengracht
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
 * MollenOS MCore - Threading Support Definitions & Structures
 * - This header describes the base threading-structures, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#include <os/mollenos.h>
#include <os/syscall.h>
#include <os/spinlock.h>
#include <ds/collection.h>
#include <threads.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include "../../libc/locale/setlocale.h"
#include "tls.h"

#define TLS_MAX_KEYS			64

/* TlsThreadInstance (Private)
 * Contains a thread-specific storage for a given
 * process-key and a thread-specific destructor. */
typedef struct _TlsThreadInstance {
    tss_t                Key;
    void                *Value;
    tss_dtor_t           Destructor;
} TlsThreadInstance_t;

/* _TlsThreadAtExit (Private)
 * Contains a thread-specific storage for a given
 * thread-key and a thread-specific destructor. */
typedef struct _TlsThreadAtExit {
    thrd_t               Key;
    void                *DsoHandle;
    void                *Value;
    tss_dtor_t           Destructor;
} TlsThreadAtExit_t;

/* TlsProcessInstance (Private)
 * Per-process TLS data that stores the
 * allocated keys and their desctructors. 
 * Also keeps a list of tls-entries for threads */
typedef struct _TlsProcessInstance {
    int                 Keys[TLS_MAX_KEYS];
    tss_dtor_t          Dss[TLS_MAX_KEYS];
    Collection_t*       Tls;
    Collection_t*       TlsAtExit;
} TlsProcessInstance_t;

/* Globals 
 * Keeps track of the TLS data */
static TlsProcessInstance_t TlsGlobal;
static Spinlock_t           TlsLock;

/* tls_initialize
 * Initialises the TLS and allocates resources needed. 
 * Part of CRT initializaiton routines */
OsStatus_t
tls_initialize(void)
{
    // Reset the keys and destructors
    for (int i = 0; i < TLS_MAX_KEYS; i++) {
        TlsGlobal.Keys[i] = 0;
        TlsGlobal.Dss[i] = NULL;
    }

    // Initialize the list of thread-instances
    TlsGlobal.Tls = CollectionCreate(KeyInteger);
    TlsGlobal.TlsAtExit = CollectionCreate(KeyInteger);
    return SpinlockReset(&TlsLock);
}

/* tls_callback
 * Runs all destructors for a given thread */
void
tls_callback(
    _In_ void *Data, 
    _In_ int Index, 
    _In_ void *Userdata)
{
    // Variables
    TlsThreadInstance_t *Tls = (TlsThreadInstance_t*)Data;
    int *ValuesLeft = (int*)Userdata;

    // We don't need the index value
    _CRT_UNUSED(Index);

    // Determine whether or not we should run the destructor
    // for this tls-key
    if (Tls->Value != NULL && Tls->Destructor != NULL) {
        void *ArgValue = Tls->Value;
        Tls->Value = NULL;
        Tls->Destructor(ArgValue);

        // If the value has been updated, we need another pass
        if (Tls->Value != NULL) {
            (*ValuesLeft)++;
        }
    }
}

/* tls_atexit
 * Registers a thread-specific at-exit handler. */
void
tls_atexit(
    _In_ thrd_t thr,
    _In_ void (*function)(void*),
    _In_ void *argument,
    _In_ void *dso_symbol) // dso_handle symbol
{
    // Variables
    TlsThreadAtExit_t *Function = NULL;
    DataKey_t Key;

    // Init key
    Key.Value = thr;

    // Allocate a new instance
    Function = (TlsThreadAtExit_t*)malloc(sizeof(TlsThreadAtExit_t));
    Function->Key = thr;
    Function->Destructor = function;
    Function->Value = argument;
    Function->DsoHandle = dso_symbol;

    // Append to list
    SpinlockAcquire(&TlsLock);
    CollectionAppend(TlsGlobal.TlsAtExit, CollectionCreateNode(Key, Function));
    SpinlockRelease(&TlsLock);
}

/* tls_callatexit
 * Invokes all thread-specific at-exit handlers registered. */
OsStatus_t
tls_callatexit(
    _In_ thrd_t thr,
    _In_ void *dso_symbol) // dso_handle symbol
{
    // Variables
    DataKey_t Key;

    // Iterate and call at-exit handlers for thread
    foreach (Node, TlsGlobal.TlsAtExit) {
        TlsThreadAtExit_t *Function = (TlsThreadAtExit_t*)Node->Data;
        if (Function->Key == thr 
            && (dso_symbol == NULL || dso_symbol == Function->DsoHandle)) {
            Function->Destructor(Function->Value);
        }
    }
    
    // Init key
    Key.Value = thr;

    // Cleanup handlers
    SpinlockAcquire(&TlsLock);
    while (CollectionRemoveByKey(TlsGlobal.TlsAtExit, Key) == OsSuccess);
    return SpinlockRelease(&TlsLock);
}

/* tls_cleanup
 * Destroys the TLS for the specific thread
 * by freeing resources and calling c11 destructors. */
OsStatus_t
tls_cleanup(
    _In_ thrd_t thr)
{
    // Variables
    int NumberOfPassesLeft  = TSS_DTOR_ITERATIONS;
    int NumberOfValsLeft    = 0;
    DataKey_t Key;

    // Init key
    Key.Value = thr;

    // Execute all stored destructors untill there is no
    // more values left or we reach the maximum number of passes
    CollectionExecuteOnKey(TlsGlobal.Tls, tls_callback, Key, &NumberOfValsLeft);
    while (NumberOfValsLeft != 0 && NumberOfPassesLeft) {
        NumberOfValsLeft = 0;
        CollectionExecuteOnKey(TlsGlobal.Tls, tls_callback, Key, &NumberOfValsLeft);
        NumberOfPassesLeft--;
    }

    // Cleanup all stored tls-keys by this thread
    SpinlockAcquire(&TlsLock);
    while (CollectionRemoveByKey(TlsGlobal.Tls, Key) == OsSuccess);
    SpinlockRelease(&TlsLock);

    // Invoke at-exit
    return tls_callatexit(thr, NULL);
}

/* tls_create
 * Initializes a new thread-storage space for the caller thread.
 * Part of CRT initialization routines. */
OsStatus_t 
tls_create(
    _In_ thread_storage_t *Tls)
{
    // Clean out the tls
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
    Tls->transfer_buffer = CreateBuffer(UUID_INVALID, BUFSIZ);
    return OsSuccess;
}

/* tls_destroy
 * Destroys a thread-storage space should be called by thread crt */
OsStatus_t
tls_destroy(
    _In_ thread_storage_t *Tls)
{
    // Cleanup the transfer buffer if allocated
    if (Tls->transfer_buffer != NULL) {
        DestroyBuffer(Tls->transfer_buffer);
    }
    return OsSuccess;
}

/* tls_current 
 * Retrieves the local storage space for the current thread */
thread_storage_t*
tls_current(void)
{
    // Return reserved pointer 0
    return (thread_storage_t*)__get_reserved(0);
}

/* tss_create
 * Creates new thread-specific storage key and stores it in the object pointed to by tss_key. 
 * Although the same key value may be used by different threads, 
 * the values bound to the key by tss_set are maintained on a per-thread 
 * basis and persist for the life of the calling thread. */
int
tss_create(
    _In_ tss_t* tss_key,
    _In_ tss_dtor_t destructor)
{
    // Variables
    tss_t Result    = TSS_KEY_INVALID;
    int i;

    // Iterate and find an unallocated key
    SpinlockAcquire(&TlsLock);
    for (i = 0; i < TLS_MAX_KEYS; i++) {
        if (TlsGlobal.Keys[i] == 0) {
            TlsGlobal.Keys[i]   = 1;
            TlsGlobal.Dss[i]    = destructor;
            Result              = (tss_t)i;
            break;
        }
    }
    SpinlockRelease(&TlsLock);

    // If we reach here all keys are allocated
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
    // Variables
    CollectionItem_t *tNode = NULL;

    // Sanitize the key given
    if (tss_id >= TLS_MAX_KEYS) {
        return;
    }

    // Iterate nodes without auto-linking, we do that
    // manually
    SpinlockAcquire(&TlsLock);
    _foreach_nolink(tNode, TlsGlobal.Tls) {
        TlsThreadInstance_t *Tls = 
            (TlsThreadInstance_t*)tNode->Data;

        // Make sure we delete all instances of the key
        // If we find one, we need to unlink it and get it's
        // successor before destroying the node
        if (Tls->Key == tss_id) {
            CollectionItem_t *Old = tNode;
            tNode = CollectionUnlinkNode(TlsGlobal.Tls, tNode);
            CollectionDestroyNode(TlsGlobal.Tls, Old);
            free(Tls);
        }
        else {
            tNode = tNode->Link;
        }
    }
    // Free the key-entry
    TlsGlobal.Keys[tss_id] = 0;
    TlsGlobal.Dss[tss_id] = NULL;
    SpinlockRelease(&TlsLock);
}

/* tss_get
 * Returns the value held in thread-specific storage for the current thread 
 * identified by tss_key. Different threads may get different values identified by the same key. */
void*
tss_get(
    _In_ tss_t tss_key)
{
    // Variables
    CollectionItem_t *tNode = NULL;
    void *Result            = NULL;
    thrd_t thr              = UUID_INVALID;
    DataKey_t tKey;

    // Sanitize parameter
    if (tss_key >= TLS_MAX_KEYS) {
        return NULL;
    }

    // Initialize variables
    thr = thrd_current();
    tKey.Value = thr;

    // Iterate the list of TLS instances and 
    // find the one that contains the tls-key
    SpinlockAcquire(&TlsLock);
    _foreach(tNode, TlsGlobal.Tls) {
        TlsThreadInstance_t *Tls = (TlsThreadInstance_t*)tNode->Data;
        if (!dsmatchkey(KeyInteger, tKey, tNode->Key) && Tls->Key == tss_key) {
            Result = Tls->Value;
            break;
        }
    }
    SpinlockRelease(&TlsLock);
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
    // Variables
    TlsThreadInstance_t *NewTls = NULL;
    CollectionItem_t *tNode     = NULL;
    thrd_t thr                  = UUID_INVALID;
    DataKey_t tKey;

    // Sanitize key value
    if (tss_id >= TLS_MAX_KEYS) {
        return thrd_error;
    }

    // Initialize variables
    thr = thrd_current();
    tKey.Value = thr;

    // Iterate and find if it
    // exists, if exists we override
    SpinlockAcquire(&TlsLock);
    _foreach(tNode, TlsGlobal.Tls) {
        TlsThreadInstance_t *Tls = (TlsThreadInstance_t*)tNode->Data;
        if (!dsmatchkey(KeyInteger, tKey, tNode->Key) && Tls->Key == tss_id) {
            Tls->Value = val;
            SpinlockRelease(&TlsLock);
            return thrd_success;
        }
    }
    SpinlockRelease(&TlsLock);

    // Allocate a new instance of a thread-instance tls
    NewTls = (TlsThreadInstance_t*)malloc(sizeof(TlsThreadInstance_t));

    // Store the data into them
    // and get a handle for the destructor
    NewTls->Key = tss_id;
    NewTls->Value = val;
    NewTls->Destructor = TlsGlobal.Dss[tss_id];

    // Last thing is to append it to the tls-list
    SpinlockAcquire(&TlsLock);
    CollectionAppend(TlsGlobal.Tls, CollectionCreateNode(tKey, NewTls));
    SpinlockRelease(&TlsLock);
    return thrd_success;
}

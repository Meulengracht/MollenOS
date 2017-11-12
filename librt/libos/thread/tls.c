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

/* Includes 
 * - System */
#include <os/mollenos.h>
#include <os/syscall.h>
#include <os/thread.h>
#include <ds/list.h>

/* Includes
 * - Library */
#include "../../libc/locale/setlocale.h"
#include <crtdefs.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <errno.h>
#include <string.h>

/* TlsThreadInstance (Private)
 * Contains a thread-specific storage for a given
 * process-key and a thread-specific destructor. */
typedef struct _TlsThreadInstance {
	TlsKey_t			 Key;
	void				*Value;
	TlsKeyDss_t			 Destructor;
} TlsThreadInstance_t;

/* TlsProcessInstance (Private
 * Per-process TLS data that stores the
 * allocated keys and their desctructors. 
 * Also keeps a list of tls-entries for threads */
typedef struct _TlsProcessInstance {
	int					 Keys[TLS_MAX_KEYS];
	TlsKeyDss_t			 Dss[TLS_MAX_KEYS];
	List_t				*Tls;
} TlsProcessInstance_t;

/* Globals 
 * Keeps track of the TLS data */
static TlsProcessInstance_t __TLSGlobal;

/* TLSInit
 * Initialises the TLS
 * and allocates resources needed. 
 * Not callable manually */
OsStatus_t
TLSInit(void)
{
	// Reset the keys and destructors
	for (int i = 0; i < TLS_MAX_KEYS; i++) {
		__TLSGlobal.Keys[i] = 0;
		__TLSGlobal.Dss[i] = NULL;
	}

	// Initialize the list of thread-instances
	__TLSGlobal.Tls = ListCreate(KeyInteger);
	return OsSuccess;
}

/* Helper Callback
 * Runs all destructors for
 * a given thread */
void
TLSCallback(
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
		if (Tls->Value != NULL)
			*ValuesLeft++;
	}
}

/* TLSCleanup
 * Destroys the TLS for the specific thread
 * by freeing resources and
 * calling c11 destructors 
 * Not callable manually */
OsStatus_t
TLSCleanup(
	_In_ UUId_t ThreadId)
{
	// Variables
	int NumberOfValsLeft = 0;
	int NumberOfPassesLeft = TLS_MAX_PASSES;
	DataKey_t Key;

	// Init key
	Key.Value = ThreadId;

	// Execute all stored destructors untill there is no
	// more values left or we reach the maximum number of passes
	ListExecuteOnKey(__TLSGlobal.Tls, TLSCallback, Key, &NumberOfValsLeft);
	while (NumberOfValsLeft != 0 && NumberOfPassesLeft) {
		NumberOfValsLeft = 0;
		ListExecuteOnKey(__TLSGlobal.Tls, TLSCallback, Key, &NumberOfValsLeft);
		NumberOfPassesLeft--;
	}

	// Cleanup all stored tls-keys by this thread
	while (ListRemoveByKey(__TLSGlobal.Tls, Key));
	return OsSuccess;
}

/* TLSInitInstance
 * Initializes a new thread-storage space
 * should be called by thread crt */
OsStatus_t 
TLSInitInstance(
	_In_ ThreadLocalStorage_t *Tls)
{
	// Clean out the tls
	memset(Tls, 0, sizeof(ThreadLocalStorage_t));

	// Store it at reserved pointer place first
    __set_reserved(0, (size_t)Tls);
    
	// Initialize members to default values
	Tls->Id = UUID_INVALID;
	Tls->Errno = EOK;
	Tls->Locale = __get_global_locale();
	Tls->Seed = 1;

	// Setup a local transfer buffer for stdio operations
	Tls->Transfer = CreateBuffer(BUFSIZ);
	return OsSuccess;
}

/* TLSDestroyInstance
 * Destroys a thread-storage space
 * should be called by thread crt */
OsStatus_t 
TLSDestroyInstance(
	_In_ ThreadLocalStorage_t *Tls)
{
	// Cleanup the transfer buffer if allocated
	if (Tls->Transfer != NULL) {
		DestroyBuffer(Tls->Transfer);
	}

	// Otherwise nothing to do here yet
	return OsSuccess;
}

/* TLSGetCurrent 
 * Retrieves the local storage space
 * for the current thread */
ThreadLocalStorage_t *
TLSGetCurrent(void)
{
	// Return reserved pointer 0
	return (ThreadLocalStorage_t*)__get_reserved(0);
}

/* TLSCreateKey
 * Create a new global TLS-key, this can be used to save
 * thread-specific data */
TlsKey_t 
TLSCreateKey(
	_In_ TlsKeyDss_t Destructor)
{
	// Variables
	int i;

	// Iterate and find an unallocated key
	for (i = 0; i < TLS_MAX_KEYS; i++) {
		if (__TLSGlobal.Keys[i] == 0) {
			__TLSGlobal.Keys[i] = 1;
			__TLSGlobal.Dss[i] = Destructor;
			_set_errno(EOK);
			return (TlsKey_t)i;
		}
	}

	// If we reach here all keys are allocated
	_set_errno(EAGAIN);
	return TLS_KEY_INVALID;
}

/* TLSDestroyKey
 * Deletes the global but thread-specific key */ 
OsStatus_t 
TLSDestroyKey(
	_In_ TlsKey_t Key)
{
	// Variables
	ListNode_t *tNode = NULL;

	// Sanitize the key given
	if (Key >= TLS_MAX_KEYS) {
		return OsError;
	}

	// Iterate nodes without auto-linking, we do that
	// manually
	_foreach_nolink(tNode, __TLSGlobal.Tls) {
		TlsThreadInstance_t *Tls = 
			(TlsThreadInstance_t*)tNode->Data;

		// Make sure we delete all instances of the key
		// If we find one, we need to unlink it and get it's
		// successor before destroying the node
		if (Tls->Key == Key) {
			ListNode_t *Old = tNode;
			free(Tls);
			tNode = ListUnlinkNode(__TLSGlobal.Tls, tNode);
			ListDestroyNode(__TLSGlobal.Tls, Old);
		}
		else {
			tNode = tNode->Link;
		}
	}

	// Free the key-entry
	__TLSGlobal.Keys[Key] = 0;
	__TLSGlobal.Dss[Key] = NULL;
	return OsSuccess;
}

/* TLSGetKey
 * Get a key from the TLS and returns it's value
 * will return NULL if not exists and set errno */
void *
TLSGetKey(
	_In_ TlsKey_t Key)
{
	// Variables
	ListNode_t *tNode = NULL;
	UUId_t ThreadId;
	DataKey_t tKey;

	// Sanitize parameter
	if (Key >= TLS_MAX_KEYS) {
		return NULL;
	}

	// Initialize variables
	ThreadId = ThreadGetId();
	tKey.Value = ThreadId;

	// Iterate the list of TLS instances and 
	// find the one that contains the tls-key
	_foreach(tNode, __TLSGlobal.Tls) {
		TlsThreadInstance_t *Tls = (TlsThreadInstance_t*)tNode->Data;
		if (!dsmatchkey(KeyInteger, tKey, tNode->Key) && Tls->Key == Key) {
			return Tls->Value;
		}
	}

	// We didn't find it, return NULL
	return NULL;
}

/* TLSSetKey
 * Set a key in the TLS
 * and associates the given data with the key */
OsStatus_t 
TLSSetKey(
	_In_ TlsKey_t Key, 
	_In_Opt_ void *Data)
{
	// Variables
	TlsThreadInstance_t *NewTls = NULL;
	ListNode_t *tNode = NULL;
	UUId_t ThreadId;
	DataKey_t tKey;

	// Sanitize key value
	if (Key >= TLS_MAX_KEYS) {
		return OsError;
	}

	// Initialize variables
	ThreadId = ThreadGetId();
	tKey.Value = ThreadId;

	// Iterate and find if it
	// exists, if exists we override
	_foreach(tNode, __TLSGlobal.Tls) {
		TlsThreadInstance_t *Tls = (TlsThreadInstance_t*)tNode->Data;
		if (!dsmatchkey(KeyInteger, tKey, tNode->Key) && Tls->Key == Key) {
			Tls->Value = Data;
			return OsSuccess;
		}
	}

	// Allocate a new instance of a thread-instance tls
	NewTls = (TlsThreadInstance_t*)malloc(sizeof(TlsThreadInstance_t));

	// Store the data into them
	// and get a handle for the destructor
	NewTls->Key = Key;
	NewTls->Value = Data;
	NewTls->Destructor = __TLSGlobal.Dss[Key];

	// Last thing is to append it to the tls-list
	return ListAppend(__TLSGlobal.Tls, ListCreateNode(tKey, tKey, Data));
}

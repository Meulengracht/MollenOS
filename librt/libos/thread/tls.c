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
#include <os/mollenOS.h>
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

/* TLS Entry for threads 
 * keys are shared all over 
 * the place but values are t-specific */
typedef struct _m_tls_entry {
	TlsKey_t			 Key;
	void				*Value;
	TlsKeyDss_t			 Destructor;
} m_tls_entry;

/* The local thread storage
* space used by mollenos
* to keep track of state
* this structure is kept PER PROCESS */
typedef struct _m_tls_process {
	int					 Keys[TLS_MAX_KEYS];
	TlsKeyDss_t			 Dss[TLS_MAX_KEYS];
	List_t				*Tls;
} m_tls_process_t;

/* Globals */
m_tls_process_t __TLSGlobal;

/* TLSInit
 * Initialises the TLS
 * and allocates resources needed. 
 * Not callable manually */
OsStatus_t
TLSInit(void)
{
	/* Setup all keys to 0 */
	for (int i = 0; i < TLS_MAX_KEYS; i++) {
		__TLSGlobal.Keys[i] = 0;
		__TLSGlobal.Dss[i] = NULL;
	}

	/* Instantiate the lists */
	__TLSGlobal.Tls = ListCreate(KeyInteger, LIST_NORMAL);
	return OsNoError;
}

/* Helper Callback
 * Runs all destructors for
 * a given thread */
void TLSCallback(void *Data, int n, void *Userdata)
{
	/* Cast */
	m_tls_entry *Tls = (m_tls_entry*)Data;
	int *ValuesLeft = (int*)Userdata;

	/* Lol */
	_CRT_UNUSED(n);

	/* Run destructor? */
	if (Tls->Value != NULL
		&& Tls->Destructor != NULL) {
		void *ArgValue = Tls->Value;
		Tls->Value = NULL;
		Tls->Destructor(ArgValue);
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
	/* Setup key */
	int NumberOfValsLeft = 0;
	DataKey_t Key;
	Key.Value = ThreadId;

	/* Run all dctors */
	ListExecuteOnKey(__TLSGlobal.Tls, TLSCallback, Key, &NumberOfValsLeft);
	while (NumberOfValsLeft != 0) {
		NumberOfValsLeft = 0;
		ListExecuteOnKey(__TLSGlobal.Tls, TLSCallback, Key, &NumberOfValsLeft);
	}

	/* Remove all nodes by thread id */
	while (ListRemoveByKey(__TLSGlobal.Tls, Key));
	return OsNoError;
}

/* TLSInitInstance
 * Initializes a new thread-storage space
 * should be called by thread crt */
OsStatus_t 
TLSInitInstance(
	_In_ ThreadLocalStorage_t *Tls)
{
	/* Setup base stuff */
	memset(Tls, 0, sizeof(ThreadLocalStorage_t));

	/* Setup c-library stuff */
	Tls->Id = UUID_INVALID;
	Tls->Errno = EOK;
	Tls->Locale = __get_global_locale();
	Tls->Seed = 1;

	/* Setup a local transfer buffer of
	 * size BUFSIZE */
	Tls->Transfer = CreateBuffer(BUFSIZ);

	/* Set pointer */
	__set_reserved(0, (size_t)Tls);
	return OsNoError;
}

/* TLSDestroyInstance
 * Destroys a thread-storage space
 * should be called by thread crt */
OsStatus_t 
TLSDestroyInstance(
	_In_ ThreadLocalStorage_t *Tls)
{
	/* Cleanup buffer */
	if (Tls->Transfer != NULL) {
		DestroyBuffer(Tls->Transfer);
	}

	/* Nothing to do here yet */
	Tls->Id = 0;
	return OsNoError;
}

/* TLSGetCurrent 
 * Retrieves the local storage space
 * for the current thread */
ThreadLocalStorage_t *
TLSGetCurrent(void)
{
	return (ThreadLocalStorage_t*)__get_reserved(0);
}

/* TLSCreateKey
 * Create a new global TLS-key, this can be used to save
 * thread-specific data */
TlsKey_t 
TLSCreateKey(
	_In_ TlsKeyDss_t Destructor)
{
	/* Variables */
	int i;

	/* Iterate and find a free key */
	for (i = 0; i < TLS_MAX_KEYS; i++) {
		if (__TLSGlobal.Keys[i] == 0) {
			/* Set allocated */
			__TLSGlobal.Keys[i] = 1;
			__TLSGlobal.Dss[i] = Destructor;
			_set_errno(EOK);
			return (TlsKey_t)i;
		}
	}

	/* Sanity 
	 * If we reach here no more keys */
	_set_errno(EAGAIN);
	return TLS_KEY_INVALID;
}

/* TLSDestroyKey
 * Deletes the global but thread-specific key */ 
OsStatus_t 
TLSDestroyKey(
	_In_ TlsKey_t Key)
{
	/* Variables */
	ListNode_t *tNode;

	/* Sanitize key */
	if (Key >= TLS_MAX_KEYS) {
		return OsError;
	}

	/* Iterate nodes */
	_foreach_nolink(tNode, __TLSGlobal.Tls)
	{
		/* Cast */
		m_tls_entry *Tls = (m_tls_entry*)tNode->Data;

		/* We delete all instances of the key */
		if (Tls->Key == Key) 
		{
			/* Store */
			ListNode_t *Old = tNode;

			/* Cleanup tls entry */
			free(Tls);

			/* Get next node */
			tNode = ListUnlinkNode(__TLSGlobal.Tls, tNode);

			/* Cleanup */
			ListDestroyNode(__TLSGlobal.Tls, Old);
		}
		else {
			/* Get next node */
			tNode = tNode->Link;
		}
	}

	/* Free Key */
	__TLSGlobal.Keys[Key] = 0;
	__TLSGlobal.Dss[Key] = NULL;
	return OsNoError;
}

/* TLSGetKey
 * Get a key from the TLS and returns it's value
 * will return NULL if not exists and set errno */
void *
TLSGetKey(
	_In_ TlsKey_t Key)
{
	/* Sanitize key */
	if (Key >= TLS_MAX_KEYS)
		return NULL;

	/* Get thread id */
	ListNode_t *tNode;
	DataKey_t tKey;
	UUId_t ThreadId = ThreadGetId();

	/* Setup Key */
	tKey.Value = ThreadId;

	// Iterate the list of TLS instances and 
	// find the one that contains the tls-key
	_foreach(tNode, __TLSGlobal.Tls) {
		m_tls_entry *Tls = (m_tls_entry*)tNode->Data;
		if (!dsmatchkey(KeyInteger, tKey, tNode->Key)
			&& Tls->Key == Key) {
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
	/* Sanitize key */
	if (Key >= TLS_MAX_KEYS) {
		return OsError;
	}

	/* Get thread id */
	m_tls_entry *NewTls;
	ListNode_t *tNode;
	DataKey_t tKey;
	UUId_t ThreadId = ThreadGetId();

	/* Setup Key */
	tKey.Value = ThreadId;

	// Iterate and find if it
	// exists, if exists we override
	_foreach(tNode, __TLSGlobal.Tls) {
		m_tls_entry *Tls = (m_tls_entry*)tNode->Data;
		if (!dsmatchkey(KeyInteger, tKey, tNode->Key)
			&& Tls->Key == Key) {
			Tls->Value = Data;
			return OsNoError;
		}
	}

	/* Create a new entry */
	NewTls = (m_tls_entry*)malloc(sizeof(m_tls_entry));

	/* Store the data into them
	 * and get a handle for the destructor */
	NewTls->Key = Key;
	NewTls->Value = Data;
	NewTls->Destructor = __TLSGlobal.Dss[Key];

	/* Add it to list */
	ListAppend(__TLSGlobal.Tls, ListCreateNode(tKey, tKey, Data));
	return OsNoError;
}

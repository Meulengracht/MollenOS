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
 * MollenOS - Threading Functions
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
#include <assert.h>
#include <errno.h>
#include <string.h>

/* Read and write the magic tls thread-specific
 * pointer, we need to take into account the compiler here */
#ifdef _MSC_VER
static __CRT_INLINE size_t __get_reserved(size_t index) {
	size_t result = 0;
	size_t base = (0 - ((index * sizeof(size_t)) + sizeof(size_t)));
	__asm {
		mov ebx, [base];
		mov eax, ss:[ebx];
		mov [result], eax;
	}
	return result;
}
static __CRT_INLINE void __set_reserved(size_t index, size_t value) {
	size_t base = (0 - ((index * sizeof(size_t)) + sizeof(size_t)));
	__asm {
		mov ebx, [base];
		mov eax, [value];
		mov ss:[ebx], eax;
	}
}
#else
#error "Implement rw for tls"
#endif

/* TLS Entry for threads 
 * keys are shared all over 
 * the place but values are t-specific */
typedef struct _m_tls_entry
{
	/* This is used explicitly
	* for c11 tls implementation */
	TlsKey_t Key;
	void *Value;
	TlsKeyDss_t Destructor;

} m_tls_entry;

/* The local thread storage
* space used by mollenos
* to keep track of state
* this structure is kept PER PROCESS */
typedef struct _m_tls_process
{
	/* This is used explicitly
	* for c11 tls implementation */
	int Keys[TLS_MAX_KEYS];
	TlsKeyDss_t Dss[TLS_MAX_KEYS];
	List_t *Tls;

} m_tls_process_t;

/* Globals */
m_tls_process_t __TLSGlobal;

/* Initialises the TLS 
 * and allocates resources needed. */
void TLSInit(void)
{
	/* Setup all keys to 0 */
	for (int i = 0; i < TLS_MAX_KEYS; i++) {
		__TLSGlobal.Keys[i] = 0;
		__TLSGlobal.Dss[i] = NULL;
	}

	/* Instantiate the lists */
	__TLSGlobal.Tls = ListCreate(KeyInteger, LIST_NORMAL);
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

/* Destroys the TLS for the specific thread
 * by freeing resources and 
 * calling c11 destructors */
void TLSCleanup(TId_t ThreadId)
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
}

/* TLSInitInstance
 * Initializes a new thread-storage space
 * should be called by thread crt */
void TLSInitInstance(ThreadLocalStorage_t *Tls)
{
	/* Setup base stuff */
	memset(Tls, 0, sizeof(ThreadLocalStorage_t));

	/* Setup c-library stuff */
	Tls->Errno = EOK;
	Tls->Locale = __get_global_locale();
	Tls->Seed = 1;

	/* Set pointer */
	__set_reserved(0, (size_t)Tls);
}

/* TLSDestroyInstance
 * Destroys a thread-storage space
 * should be called by thread crt */
void TLSDestroyInstance(ThreadLocalStorage_t *Tls)
{
	/* Nothing to do here yet */
	Tls->Id = 0;
}

/* TLSGetCurrent 
 * Retrieves the local storage space
 * for the current thread */
ThreadLocalStorage_t *TLSGetCurrent(void)
{
	/* Done */
	return (ThreadLocalStorage_t*)__get_reserved(0);
}

/* Create a new global
 * TLS-key, this can be used to save
 * thread-specific data */
TlsKey_t TLSCreateKey(TlsKeyDss_t Destructor)
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
	return 0xFFFFFFFF;
}

/* Deletes the global
 * but thread-specific key */
void TLSDestroyKey(TlsKey_t Key)
{
	/* Variables */
	ListNode_t *tNode;

	/* Sanitize key */
	if (Key >= TLS_MAX_KEYS)
		return;

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
}

/* Get a key from the TLS
 * and returns it's value
 * will return NULL if not exists
 * and set errno */
void *TLSGetKey(TlsKey_t Key)
{
	/* Sanitize key */
	if (Key >= TLS_MAX_KEYS)
		return NULL;

	/* Get thread id */
	ListNode_t *tNode;
	DataKey_t tKey;
	TId_t ThreadId = ThreadGetCurrentId();

	/* Setup Key */
	tKey.Value = ThreadId;

	/* Iterate and find */
	_foreach(tNode, __TLSGlobal.Tls)
	{
		/* Cast */
		m_tls_entry *Tls = (m_tls_entry*)tNode->Data;

		/* We delete all instances of the key */
		if (!dsmatchkey(__TLSGlobal.Tls->KeyType, tKey, tNode->Key)
			&& Tls->Key == Key) {
			return Tls->Value;
		}
	}

	/* Damn, not found... */
	return NULL;
}

/* Set a key in the TLS
 * and associates the given
 * data with the key */
int TLSSetKey(TlsKey_t Key, void *Data)
{
	/* Sanitize key */
	if (Key >= TLS_MAX_KEYS)
		return -1;

	/* Get thread id */
	m_tls_entry *NewTls;
	ListNode_t *tNode;
	DataKey_t tKey;
	TId_t ThreadId = ThreadGetCurrentId();

	/* Setup Key */
	tKey.Value = ThreadId;

	/* Iterate and find if it
	 * exists, if exists we override */
	_foreach(tNode, __TLSGlobal.Tls)
	{
		/* Cast */
		m_tls_entry *Tls = (m_tls_entry*)tNode->Data;

		/* We delete all instances of the key */
		if (!dsmatchkey(__TLSGlobal.Tls->KeyType, tKey, tNode->Key)
			&& Tls->Key == Key) {
			Tls->Value = Data;
			return 0;
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

	/* Done!! */
	return 0;
}

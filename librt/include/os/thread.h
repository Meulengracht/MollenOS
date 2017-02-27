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

#if !defined(_THREADING_INTERFACE_H_) && !defined(CLIB_KERNEL)
#define _THREADING_INTERFACE_H_

/* Includes
 * - Library */
#include <os/osdefs.h>
#include <sys/types.h>
#include <time.h>

/* Includes
 * - System */
#include <os/driver/buffer.h>
#include <os/spinlock.h>
#include <os/mutex.h>
#include <os/condition.h>

/* The definition of a thread id
 * used for identifying threads */
typedef long ThreadOnce_t;
typedef void(*ThreadOnceFunc_t)(void);

/* The definition of a thread specifc
 * storage key, used for saving data
 * in a dictionary */
typedef unsigned int TlsKey_t;
typedef void(*TlsKeyDss_t)(void*);

/* The definition of a thread entry point format */
typedef int(*ThreadFunc_t)(void*);

/* Thread and TLS Definitons */
#define THREAD_ONCE_INIT		0x1

#define TLS_MAX_KEYS			64
#define TLS_KEY_INVALID			0xFFFFFFFF

/* The actual TLS
 * This is the actual thread local storage
 * that is created per thread and is purely
 * acccessible by the local thread only */
PACKED_TYPESTRUCT(ThreadLocalStorage, {
	UUId_t					 Id;
	void					*Handle;
	errno_t					 Errno;
	void					*Locale;
	unsigned int			 Seed;
	char					*StrTokNext;
	struct tm				 TmBuffer;
	char					 AscBuffer[26];
	BufferObject_t			*Transfer;

	void					*TerminateHandler;
	void					*UnexpectedHandler;
	void					*SeTranslator;
	void					*ExceptionInfo;
	void					*ExceptionRecord;
	void					*ExceptionList;
	void					*StackLow;
	void					*StackHigh;
	int						 IsDebugging;
});

/* Start one of these before function prototypes */
_CODE_BEGIN

/* ThreadOnce
 * This is a support thread function
 * that makes sure that even with shared
 * functions between threads a function
 * only ever gets called once */
_MOS_API 
void 
ThreadOnce(
	_In_ ThreadOnce_t *Control,
	_In_ ThreadOnceFunc_t Function);

/* ThreadCreate
 * Creates a new thread bound to 
 * the calling process, with the given
 * entry point and arguments */
_MOS_API 
UUId_t 
ThreadCreate(
	_In_ ThreadFunc_t Entry, 
	_In_Opt_ void *Data);

/* ThreadExit
 * Exits the current thread and 
 * instantly yields control to scheduler */
_MOS_API 
void 
ThreadExit(
	_In_ int ExitCode);

/* ThreadJoin
 * waits for a given thread to finish executing, and
 * returns it's exit code, Must be in same
 * process as asking thread */
_MOS_API 
int 
ThreadJoin(
	_In_ UUId_t ThreadId);

/* ThreadKill
 * Thread kill, kills the given thread
 * id, must belong to same process as the
 * thread that asks. */
_MOS_API 
OsStatus_t 
ThreadKill(
	_In_ UUId_t ThreadId);

/* ThreadSleep
 * Sleeps the current thread for the
 * given milliseconds. */
_MOS_API 
void 
ThreadSleep(
	_In_ size_t MilliSeconds);

/* ThreadGetCurrentId
 * Retrieves the current thread id */
_MOS_API 
UUId_t 
ThreadGetCurrentId(void);

/* ThreadYield
 * This yields the current thread 
 * and gives cpu time to another thread */
_MOS_API 
void 
ThreadYield(void);

/* TLSInit
 * Initialises the TLS
 * and allocates resources needed. 
 * Not callable manually */
_MOS_API 
OsStatus_t
TLSInit(void);

/* TLSCleanup
 * Destroys the TLS for the specific thread
 * by freeing resources and
 * calling c11 destructors 
 * Not callable manually */
__EXTERN
OsStatus_t
TLSCleanup(
	_In_ UUId_t ThreadId);

/* TLSInitInstance
 * Initializes a new thread-storage space
 * should be called by thread crt */
_MOS_API 
OsStatus_t 
TLSInitInstance(
	_In_ ThreadLocalStorage_t *Tls);

/* TLSDestroyInstance
 * Destroys a thread-storage space
 * should be called by thread crt */
_MOS_API 
OsStatus_t 
TLSDestroyInstance(
	_In_ ThreadLocalStorage_t *Tls);

/* TLSGetCurrent 
 * Retrieves the local storage space
 * for the current thread */
_MOS_API 
ThreadLocalStorage_t *
TLSGetCurrent(void);

/* TLSCreateKey
 * Create a new global TLS-key, this can be used to save
 * thread-specific data */
_MOS_API 
TlsKey_t 
TLSCreateKey(
	_In_ TlsKeyDss_t Destructor);

/* TLSDestroyKey
 * Deletes the global but thread-specific key */
_MOS_API 
OsStatus_t 
TLSDestroyKey(
	_In_ TlsKey_t Key);

/* TLSGetKey
 * Get a key from the TLS and returns it's value
 * will return NULL if not exists and set errno */
_MOS_API 
void *
TLSGetKey(
	_In_ TlsKey_t Key);

/* TLSSetKey
 * Set a key in the TLS
 * and associates the given data with the key */
_MOS_API 
OsStatus_t 
TLSSetKey(
	_In_ TlsKey_t Key, 
	_In_Opt_ void *Data);

_CODE_END

#endif //!_THREADING_INTERFACE_H_

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

#if !defined(_THREADING_INTERFACE_H_)
#define _THREADING_INTERFACE_H_

/* Includes
 * - Library */
#include <os/osdefs.h>
#include <sys/types.h>
#include <time.h>
#include <wchar.h>

/* Includes
 * - System */
#include <os/driver/buffer.h>

/* ThreadOnce Library Definitions
 * The definition of a thread id used for identifying threads */
typedef long ThreadOnce_t;
typedef void(*ThreadOnceFunc_t)(void);
#define THREAD_ONCE_INIT		0x1

/* Thread TLS Library Definitions 
 * The definition of a thread specifc storage key, 
 * used for saving data in a dictionary */
typedef unsigned int TlsKey_t;
typedef void(*TlsKeyDss_t)(void*);
#define TLS_MAX_PASSES			4
#define TLS_MAX_KEYS			64
#define TLS_KEY_INVALID			0xFFFFFFFF

/* Thread Library Definitions 
 * Includes the prototype of a thread entry point */
typedef int(*ThreadFunc_t)(void*);

/* Thread Local Storage
 * This is the structure that exists seperately for each running
 * thread, and can be retrieved with TLSGetCurrent() which returns
 * the local copy of this structure */
PACKED_TYPESTRUCT(ThreadLocalStorage, {
	UUId_t					 Id;
	void					*Handle;
	errno_t					 Errno;
	void					*Locale;
	mbstate_t				 MbState;
	unsigned int			 Seed;
	char					*StrTokNext;
	struct tm				 TmBuffer;
	char					 AscBuffer[26];
	BufferObject_t			*Transfer;

	// Exception & RTTI Support
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
 * Executes the ThreadOnceFunc_t object exactly once, even if 
 * called from several threads. */
MOSAPI
void
MOSABI
ThreadOnce(
	_In_ ThreadOnce_t *Control,
	_In_ ThreadOnceFunc_t Function);

/* ThreadCreate
 * Creates a new thread executing the given function and with the
 * given arguments. The id of the new thread is returned. */
MOSAPI 
UUId_t
MOSABI
ThreadCreate(
	_In_ ThreadFunc_t Entry, 
	_In_Opt_ void *Data);

/* ThreadExit
 * Signals to the operating system that the caller thread is now
 * done and can be cleaned up. This does not terminate the process
 * unless it's the last thread alive */
MOSAPI
OsStatus_t
MOSABI
ThreadExit(
	_In_ int ExitCode);

/* ThreadJoin
 * Waits for the given thread to finish executing. The returned value
 * is either the return-code from the running thread or -1 in case of
 * invalid thread-id. */
MOSAPI 
int
MOSABI
ThreadJoin(
	_In_ UUId_t ThreadId);

/* ThreadSignal
 * Invokes a signal on the given thread id, for security reasons
 * it's only possible to signal threads local to the running process. */
MOSAPI
OsStatus_t
MOSABI
ThreadSignal(
	_In_ UUId_t ThreadId,
	_In_ int SignalCode);

/* ThreadKill
 * Kill's the given thread by sending a SIGKILL to the thread, forcing
 * it to run cleanup and terminate. Thread might not terminate immediately. */
MOSAPI
OsStatus_t
MOSABI
ThreadKill(
	_In_ UUId_t ThreadId);

/* ThreadSleep
 * Sleeps the current thread for the given milliseconds. */
MOSAPI
void
MOSABI
ThreadSleep(
	_In_ size_t MilliSeconds);

/* ThreadGetId
 * Retrieves the thread id of the calling thread. */
MOSAPI
UUId_t
MOSABI
ThreadGetId(void);

/* ThreadYield
 * Signals to the operating system that this thread can be yielded
 * and will wait another turn before proceeding. */
MOSAPI 
void
MOSABI
ThreadYield(void);

/* TLSInit
 * Initialises the TLS and allocates resources needed. 
 * Not callable manually */
MOSAPI
OsStatus_t
MOSABI
TLSInit(void);

/* TLSCleanup
 * Destroys the TLS for the specific thread
 * by freeing resources and calling c11 destructors 
 * Not callable manually */
__EXTERN
OsStatus_t
MOSABI
TLSCleanup(
	_In_ UUId_t ThreadId);

/* TLSInitInstance
 * Initializes a new thread-storage space
 * should be called by thread crt */
MOSAPI 
OsStatus_t
MOSABI
TLSInitInstance(
	_In_ ThreadLocalStorage_t *Tls);

/* TLSDestroyInstance
 * Destroys a thread-storage space
 * should be called by thread crt */
MOSAPI
OsStatus_t
MOSABI
TLSDestroyInstance(
	_In_ ThreadLocalStorage_t *Tls);

/* TLSGetCurrent 
 * Retrieves the local storage space
 * for the current thread */
MOSAPI 
ThreadLocalStorage_t*
MOSABI
TLSGetCurrent(void);

/* TLSCreateKey
 * Create a new global TLS-key, this can be used to save
 * thread-specific data */
MOSAPI
TlsKey_t
MOSABI
TLSCreateKey(
	_In_ TlsKeyDss_t Destructor);

/* TLSDestroyKey
 * Deletes the global but thread-specific key */
MOSAPI
OsStatus_t
MOSABI
TLSDestroyKey(
	_In_ TlsKey_t Key);

/* TLSGetKey
 * Get a key from the TLS and returns it's value
 * will return NULL if not exists and set errno */
MOSAPI 
void*
MOSABI
TLSGetKey(
	_In_ TlsKey_t Key);

/* TLSSetKey
 * Set a key in the TLS
 * and associates the given data with the key */
MOSAPI
OsStatus_t
MOSABI
TLSSetKey(
	_In_ TlsKey_t Key, 
	_In_Opt_ void *Data);

_CODE_END

#endif //!_THREADING_INTERFACE_H_

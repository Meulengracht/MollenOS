/* MollenOS
 *
 * Copyright 2011 - 2016, Philip Meulengracht
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
 * MollenOS C Library - Standard OS Threading Header
 * Contains threading methods + synchronization
 */

#if !defined(__THREADING_CLIB__) && !defined(CLIB_KERNEL)
#define __THREADING_CLIB__

/* C-Library - Includes */
#include <sys/types.h>
#include <time.h>
#include <crtdefs.h>
#include <stdint.h>
#include <os/MollenOS.h>

/* Synchronizations */
#include <os/Spinlock.h>
#include <os/Mutex.h>
#include <os/Condition.h>

/* CPP-Guard */
#ifdef __cplusplus
extern "C" {
#endif

/* Definitons */
#define THREAD_ONCE_INIT		0x1

#define TLS_MAX_KEYS			64
#define TLS_KEY_INVALID			0xFFFFFFFF

/***********************
 * Structures
 ***********************/

/* The actual TLS
 * This is the actual thread local storage
 * that is created per thread and is purely
 * acccessible by the local thread only */
typedef struct _ThreadLocalStorage 
{
	/* Thread Information */
	TId_t			 ThreadId;

	/* C Library stuff */
	void			*ThreadHandle;
	errno_t			 ThreadErrno;
	unsigned long	 ThreadDOSErrno;
	int              ThreadUnknown0;
	void			*ThreadLocale;

	/* Seed for rand() */
	unsigned int	 ThreadSeed;

	/* Ptr for strtok() */
	char			*StrTokNext;

	/* Buffer for time functions */
	tm				 TmBuffer;

	/* Exceptions stuff */
	void			*TerminateHandler;
	void			*UnexpectedHandler;
	void			*SeTranslator;
	void			*ExceptionInfo;
	void			*ExceptionRecord;
	void			*ExceptionList;

	/* Stack Information */
	void			*StackLow;
	void			*StackHigh;

	/* Debugging */
	int				 IsDebugging;

} ThreadLocalStorage_t;

/* Prototypes */

/***********************
 * Threading Prototypes
 ***********************/

/* This is a support thread function
 * that makes sure that even with shared
 * functions between threads a function
 * only ever gets called once */
_MOS_API void ThreadOnce(ThreadOnce_t *Control, ThreadOnceFunc_t Function);

/* Creates a new thread bound to 
 * the calling process, with the given
 * entry point and arguments */
_MOS_API TId_t ThreadCreate(ThreadFunc_t Entry, void *Data);

/* Exits the current thread and 
 * instantly yields control to scheduler */
_MOS_API void ThreadExit(int ExitCode);

/* Thread join, waits for a given
 * thread to finish executing, and
 * returns it's exit code, works 
 * like processjoin. Must be in same
 * process as asking thread */
_MOS_API int ThreadJoin(TId_t ThreadId);

/* Thread kill, kills the given thread
 * id, must belong to same process as the
 * thread that asks. */
_MOS_API int ThreadKill(TId_t ThreadId);

/* Thread sleep,
 * Sleeps the current thread for the
 * given milliseconds. */
_MOS_API void ThreadSleep(size_t MilliSeconds);

/* Thread get current id
 * Get's the current thread id */
_MOS_API TId_t ThreadGetCurrentId(void);

/* This yields the current thread 
 * and gives cpu time to another thread */
_MOS_API void ThreadYield(void);

/***********************
 * TLS Prototypes
 ***********************/

/* Initialises the TLS
 * and allocates resources needed. 
 * Not callable manually */
_MOS_API void TLSInit(void);

/* Destroys the TLS for the specific thread
 * by freeing resources and
 * calling c11 destructors 
 * Not callable manually */
EXTERN void TLSCleanup(TId_t ThreadId);

/* TLSInitInstance
 * Initializes a new thread-storage space
 * should be called by thread crt */
_MOS_API void TLSInitInstance(ThreadLocalStorage_t *Tls);

/* TLSDestroyInstance
 * Destroys a thread-storage space
 * should be called by thread crt */
_MOS_API void TLSDestroyInstance(ThreadLocalStorage_t *Tls);

/* TLSGetCurrent 
 * Retrieves the local storage space
 * for the current thread */
_MOS_API ThreadLocalStorage_t *TLSGetCurrent(void);

/* Create a new global 
 * TLS-key, this can be used to save
 * thread-specific data */
_MOS_API TlsKey_t TLSCreateKey(TlsKeyDss_t Destructor);

/* Deletes the global
 * but thread-specific key */
_MOS_API void TLSDestroyKey(TlsKey_t Key);

/* Get a key from the TLS 
 * and returns it's value
 * will return NULL if not exists 
 * and set errno */
_MOS_API void *TLSGetKey(TlsKey_t Key);

/* Set a key in the TLS
 * and associates the given
 * data with the key 
 * returns -1 if no more room for keys */
_MOS_API int TLSSetKey(TlsKey_t Key, void *Data);

/* CPP Guard */
#ifdef __cplusplus
}
#endif

#endif //!__THREADING_CLIB__
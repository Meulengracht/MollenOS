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

#ifndef __THREADING_CLIB__
#define __THREADING_CLIB__

/* C-Library - Includes */
#include <sys/types.h>
#include <crtdefs.h>
#include <stdint.h>

/* CPP-Guard */
#ifdef __cplusplus
extern "C" {
#endif

/* Definitons */
#define THREAD_ONCE_INIT		0x1

#define MUTEX_INITIALIZOR		{0, 0, 0, 0}
#define MUTEX_PLAIN				0x0
#define MUTEX_RECURSIVE			0x1
#define MUTEX_DEFAULT_TIMEOUT	500
#define MUTEX_SUCCESS			0x0
#define MUTEX_BUSY				0x1

#define TLS_MAX_KEYS			64
#define TLS_KEY_INVALID			0xFFFFFFFF

/* The definition of a thread id
 * used for identifying threads */
#ifndef MTHREADID_DEFINED
#define MTHREADID_DEFINED
typedef unsigned int TId_t;
typedef long ThreadOnce_t;
typedef void (*ThreadOnceFunc_t)(void);
#endif

/* The definition of a thread specifc
 * storage key, used for saving data
 * in a dictionary */
#ifndef MTHREADTLS_DEFINED
#define MTHREADTLS_DEFINED
typedef unsigned int TlsKey_t;
typedef void (*TlsKeyDss_t)(void*);
#endif

/* The definition of a thread entry
 * point format */
#ifndef MTHREADENTRY_DEFINED
#define MTHREADENTRY_DEFINED
typedef int (*ThreadFunc_t)(void*);
#endif

/* The definition of a spinlock handle
 * used for primitive lock access */
#ifndef MSPINLOCK_DEFINED
#define MSPINLOCK_DEFINED
typedef int Spinlock_t;
#endif

/* The definition of a condition handle
 * used for primitive lock signaling */
#ifndef MTHREADCOND_DEFINED
#define MTHREADCOND_DEFINED
typedef unsigned int Condition_t;
#endif

/***********************
 * Structures
 ***********************/

/* The mutex structure
 * used for exclusive access to a resource
 * between threads */
typedef struct _Mutex
{
	/* Mutex flags/type */
	int Flags;

	/* Task that is blocking */
	TId_t Blocker;

	/* Total amout of blocking */
	size_t Blocks;

	/* The spinlock */
	Spinlock_t Lock;

} Mutex_t;

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

	/* Seed for rand() */
	unsigned int	 ThreadSeed;

	/* Ptr for strtok() */
	char			*StrTokNext;


	/* Exceptions stuff */
	void			*TerminateHandler;
	void			*UnexpectedHandler;
	void			*SeTranslator;
	void			*ExceptionInfo;
	void			*ExceptionRecord;
	void			*ExceptionList;

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

/***********************
 * Spinlock Prototypes
 ***********************/

/* Spinlock Reset
 * This initializes a spinlock
 * handle and sets it to default
 * value (unlocked) */
_MOS_API void SpinlockReset(Spinlock_t *Lock);

/* Spinlock Acquire
 * Acquires the spinlock, this
 * is a blocking operation.
 * Returns 1 on lock success */
_MOS_API int SpinlockAcquire(Spinlock_t *Lock);

/* Spinlock TryAcquire
 * TRIES to acquires the spinlock, 
 * returns 0 if failed, returns 1
 * if lock were acquired  */
_MOS_API int SpinlockTryAcquire(Spinlock_t *Lock);

/* Spinlock Release
 * Releases the spinlock, and lets
 * other threads access the lock */
_MOS_API void SpinlockRelease(Spinlock_t *Lock);

/***********************
 * Mutex Prototypes
 ***********************/

/* Instantiates a new mutex of the given
 * type, it allocates all neccessary resources
 * as well. */
_MOS_API Mutex_t *MutexCreate(int Flags);

/* Instantiates a new mutex of the given
 * type, using pre-allocated memory */
_MOS_API void MutexConstruct(Mutex_t *Mutex, int Flags);

/* Destroys a mutex and frees resources
 * allocated by the mutex */
_MOS_API void MutexDestruct(Mutex_t *Mutex);

/* Lock a mutex, this is a
 * blocking call */
_MOS_API int MutexLock(Mutex_t *Mutex);

/* Tries to lock a mutex, if the 
 * mutex is locked, this returns 
 * MUTEX_BUSY, otherwise MUTEX_SUCCESS */
_MOS_API int MutexTryLock(Mutex_t *Mutex);

/* Tries to lock a mutex, with a timeout
 * which means it'll keep retrying locking
 * untill the time has passed */
_MOS_API int MutexTimedLock(Mutex_t *Mutex, time_t Expiration);

/* Unlocks a mutex, reducing the blocker
 * count by 1 if recursive, otherwise it opens
 * the mutex */
_MOS_API void MutexUnlock(Mutex_t *Mutex);

/***********************
 * Condition Prototypes
 ***********************/

/* Instantiates a new condition and allocates
 * all required resources for the condition */
_MOS_API Condition_t *ConditionCreate(void);

/* Constructs an already allocated condition
 * handle and initializes it */
_MOS_API int ConditionConstruct(Condition_t *Cond);

/* Destroys a conditional variable and 
 * wakes up all remaining sleepers */
_MOS_API void ConditionDestroy(Condition_t *Cond);

/* Signal the condition and wakes up a thread
 * in the queue for the condition */
_MOS_API int ConditionSignal(Condition_t *Cond);

/* Broadcast a signal to all condition waiters
 * and wakes threads up waiting for the cond */
_MOS_API int ConditionBroadcast(Condition_t *Cond);

/* Waits for condition to be signaled, and 
 * acquires the given mutex, using multiple 
 * mutexes for same condition is undefined behaviour */
_MOS_API int ConditionWait(Condition_t *Cond, Mutex_t *Mutex);

/* This functions as the ConditionWait, 
 * but also has a timeout specified, so that 
 * we get waken up if the timeout expires (in seconds) */
_MOS_API int ConditionWaitTimed(Condition_t *Cond, Mutex_t *Mutex, time_t Expiration);

/* CPP Guard */
#ifdef __cplusplus
}
#endif

#endif //!__THREADING_CLIB__
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
 * C11-Support Threading Implementation
 * - Definitions, prototypes and information needed.
 */

#ifndef __STDC_THREADS__
#define __STDC_THREADS__

#include <os/osdefs.h>
#include <limits.h>
#include <time.h>

// In non c++ mode we define thread_local keyword
#if !defined(__cplusplus)
#define thread_local _Thread_local
#endif

// Callback prototypes that should be defined in C11 threads.h
typedef int (*thrd_start_t)(void*);
typedef void (*tss_dtor_t)(void*);

// Define default threading types
typedef unsigned int tss_t;
typedef UUId_t       thrd_t;

// Condition Synchronization Object
typedef struct cnd {
    _Atomic(int) syncobject;
} cnd_t;

// Mutex Synchronization Object
typedef struct mtx {
    int          flags;
    UUId_t       owner;
    _Atomic(int) references;
    _Atomic(int) value;
} mtx_t;
// _MTX_INITIALIZER_NP

// Once-Flag Synchronization Object
typedef struct once_flag {
    mtx_t syncobject;
    int   value;
} once_flag;

enum {
    thrd_success    = 0,
    thrd_busy       = 1,
    thrd_timedout   = 2,
    thrd_nomem      = 3,
    thrd_error      = -1
};

enum {
    mtx_plain       = 0,
    mtx_recursive   = 1,
    mtx_timed       = 2
};

#define TSS_DTOR_ITERATIONS 4
#define TSS_KEY_INVALID     UINT_MAX

#if defined(__cplusplus)
#define COND_INIT           { 0 }
#define MUTEX_INIT(type)    { type, UUID_INVALID, 0, 0 }
#else
// Use stdatomic C11
#define COND_INIT           { ATOMIC_VAR_INIT(0) }
#define MUTEX_INIT(type)    { type, UUID_INVALID, ATOMIC_VAR_INIT(0), ATOMIC_VAR_INIT(0) }
#endif
#define ONCE_FLAG_INIT      { MUTEX_INIT(mtx_plain), 0 }

_CODE_BEGIN
/* call_once
 * Calls function func exactly once, even if invoked from several threads. 
 * The completion of the function func synchronizes with all previous or subsequent 
 * calls to call_once with the same flag variable. */
CRTDECL(void,
call_once(
    _In_ once_flag* flag, 
    _In_ void     (*func)(void)));

/* thrd_create
 * Creates a new thread executing the function func. The function is invoked as func(arg).
 * If successful, the object pointed to by thr is set to the identifier of the new thread.
 * The completion of this function synchronizes-with the beginning of the thread. */
CRTDECL(int,
thrd_create(
    _Out_ thrd_t*      thr,
    _In_  thrd_start_t func,
    _In_  void*        arg));

/* thrd_equal
 * Checks whether lhs and rhs refer to the same thread. */
CRTDECL(int,
thrd_equal(
    _In_ thrd_t lhs,
    _In_ thrd_t rhs));

/* thrd_current
 * Returns the identifier of the calling thread. */
CRTDECL(thrd_t,
thrd_current(void));

/* thrd_sleep
 * Blocks the execution of the current thread for at least until the TIME_UTC 
 * based time point pointed to by time_point has been reached.
 * The sleep may resume earlier if a signal that is not ignored is received. 
 * In such case, if remaining is not NULL, the remaining time duration is stored 
 * into the object pointed to by remaining. */
CRTDECL(int,
thrd_sleep(
    _In_     const struct timespec* time_point,
    _In_Opt_ struct timespec*       remaining));

/* thrd_sleep
 * Blocks the execution of the current thread for at least given milliseconds */
CRTDECL(int,
thrd_sleepex(
    _In_ size_t msec));

/* thrd_yield
 * Provides a hint to the implementation to reschedule the execution of threads, 
 * allowing other threads to run. */
CRTDECL(void,
thrd_yield(void));

/* thrd_exit
 * First, for every thread-specific storage key which was created with a non-null 
 * destructor and for which the associated value is non-null (see tss_create), thrd_exit 
 * sets the value associated with the key to NULL and then invokes the destructor with 
 * the previous value of the key. The order in which the destructors are invoked is unspecified.
 * If, after this, there remain keys with both non-null destructors and values 
 * (e.g. if a destructor executed tss_set), the process is repeated up to TSS_DTOR_ITERATIONS times.
 * Finally, the thrd_exit function terminates execution of the calling thread and sets its result code to res.
 * If the last thread in the program is terminated with thrd_exit, the entire program 
 * terminates as if by calling exit with EXIT_SUCCESS as the argument (so the functions 
 * registered by atexit are executed in the context of that last thread) */
CRTDECL(void, 
thrd_exit(
    _In_ int res));

/* thrd_detach
 * Detaches the thread identified by thr from the current environment. 
 * The resources held by the thread will be freed automatically once the thread exits. */
CRTDECL(int,
thrd_detach(
    _In_ thrd_t thr));

/* thrd_join
 * Blocks the current thread until the thread identified by thr finishes execution.
 * If res is not a null pointer, the result code of the thread is put to the location pointed to by res.
 * The termination of the thread synchronizes-with the completion of this function.
 * The behavior is undefined if the thread was previously detached or joined by another thread. */
CRTDECL(int,
thrd_join(
    _In_  thrd_t thr,
    _Out_ int*   res));

/* thrd_signal
 * Invokes a signal on the given thread id, for security reasons
 * it's only possible to signal threads local to the running process. */
CRTDECL(int,
thrd_signal(
    _In_ thrd_t thr,
    _In_ int    sig));

/* mtx_init
 * Creates a new mutex object with type. The object pointed to by mutex is set to an 
 * identifier of the newly created mutex. */
CRTDECL(int,
mtx_init(
    _In_ mtx_t* mutex,
    _In_ int    type));

/* mtx_lock 
 * Blocks the current thread until the mutex pointed to by mutex is locked.
 * The behavior is undefined if the current thread has already locked the mutex 
 * and the mutex is not recursive. */
CRTDECL(int,
mtx_lock(
    _In_ mtx_t* mutex));

/* mtx_timedlock
 * Blocks the current thread until the mutex pointed to by mutex is 
 * locked or until the TIME_UTC based time point pointed to by time_point has been reached. */
CRTDECL(int,
mtx_timedlock(
    _In_ mtx_t*restrict                  mutex,
    _In_ const struct timespec *restrict time_point));

/* mtx_trylock
 * Tries to lock the mutex pointed to by mutex without blocking. 
 * Returns immediately if the mutex is already locked. */
CRTDECL(int,
mtx_trylock(
    _In_ mtx_t* mutex));

/* mtx_unlock
 * Unlocks the mutex pointed to by mutex. */
CRTDECL(int,
mtx_unlock(
    _In_ mtx_t* mutex));

/* mtx_destroy
 * Destroys the mutex pointed to by mutex. If there are threads waiting on mutex, 
 * the behavior is undefined. */
CRTDECL(void,
mtx_destroy(
    _In_ mtx_t *mutex));

/* cnd_init
 * Initializes new condition variable. 
 * The object pointed to by cond will be set to value that identifies the condition variable. */
CRTDECL(int,
cnd_init(
    _In_ cnd_t* cond));

/* cnd_signal
 * Unblocks one thread that currently waits on condition variable pointed to by cond. 
 * If no threads are blocked, does nothing and returns thrd_success. */
CRTDECL(int,
cnd_signal(
    _In_ cnd_t* cond));

/* cnd_broadcast
 * Unblocks all thread that currently wait on condition variable pointed to by cond. 
 * If no threads are blocked, does nothing and returns thrd_success. */
CRTDECL(int, 
cnd_broadcast(
    _In_ cnd_t* cond));

/* cnd_wait
 * Atomically unlocks the mutex pointed to by mutex and blocks on the 
 * condition variable pointed to by cond until the thread is signalled 
 * by cnd_signal or cnd_broadcast. The mutex is locked again before the function returns. */
CRTDECL(int,
cnd_wait(
    _In_ cnd_t* cond,
    _In_ mtx_t* mutex));

/* cnd_timedwait
 * Atomically unlocks the mutex pointed to by mutex and blocks on the 
 * condition variable pointed to by cond until the thread is signalled 
 * by cnd_signal or cnd_broadcast, or until the TIME_UTC based time point 
 * pointed to by time_point has been reached. The mutex is locked again 
 * before the function returns. */
CRTDECL(int,
cnd_timedwait(
    _In_ cnd_t* restrict                 cond,
    _In_ mtx_t* restrict                 mutex,
    _In_ const struct timespec* restrict time_point));

/* cnd_destroy
 * Destroys the condition variable pointed to by cond. If there are threads 
 * waiting on cond, the behavior is undefined. */
CRTDECL(void,
cnd_destroy(
    _In_ cnd_t* cond));

/* tss_create
 * Creates new thread-specific storage key and stores it in the object pointed to by tss_key. 
 * Although the same key value may be used by different threads, 
 * the values bound to the key by tss_set are maintained on a per-thread 
 * basis and persist for the life of the calling thread. */
CRTDECL(int,
tss_create(
    _In_ tss_t*     tss_key,
    _In_ tss_dtor_t destructor));

/* tss_get
 * Returns the value held in thread-specific storage for the current thread 
 * identified by tss_key. Different threads may get different values identified by the same key. */
CRTDECL(void*,
tss_get(
    _In_ tss_t tss_key));

/* tss_set
 * Sets the value of the thread-specific storage identified by tss_id for the 
 * current thread to val. Different threads may set different values to the same key. */
CRTDECL(int,
tss_set(
    _In_ tss_t tss_id,
    _In_ void* val));

/* tss_delete
 * Destroys the thread-specific storage identified by tss_id. */
CRTDECL(void,
tss_delete(
    _In_ tss_t tss_id));

_CODE_END
#endif //!__STDC_THREADS__

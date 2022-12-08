/**
 * Copyright 2022, Philip Meulengracht
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
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
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
typedef uuid_t       thrd_t;

#define TSS_DTOR_ITERATIONS 4
#define TSS_KEY_INVALID     UINT_MAX

_CODE_BEGIN

#ifndef __OSCONFIG_C11_GREEN_THREADS_DISABLED
#include <errno.h>
#include <os/usched/cond.h>
#include <os/usched/mutex.h>
#include <os/usched/job.h>
#include <os/usched/once.h>

typedef struct usched_cnd cnd_t;
typedef struct usched_mtx mtx_t;
typedef struct usched_once_flag once_flag;

enum {
    thrd_success    = 0,
    thrd_busy       = 1,
    thrd_timedout   = 2,
    thrd_nomem      = 3,
    thrd_error      = -1
};

enum {
    mtx_plain       = USCHED_MUTEX_PLAIN,
    mtx_recursive   = USCHED_MUTEX_RECURSIVE,
    mtx_timed       = USCHED_MUTEX_TIMED
};

#define MUTEX_INIT(type) { type, _SPN_INITIALIZER_NP, NULL, NULL }
#define COND_INIT        { MUTEX_INIT(mtx_plain), NULL }
#define ONCE_FLAG_INIT   { MUTEX_INIT(mtx_plain), 0 }

static inline int __to_thrd_error(int err) {
    if (err == 0) {
        return thrd_success;
    }

    switch (errno) {
        case EBUSY: return thrd_busy;
        case ETIME: return thrd_timedout;
        case ENOMEM: return thrd_nomem;
        default: return ENOSYS;
    }
}

/**
 * @brief Calls function func exactly once, even if invoked from several threads.
 * The completion of the function func synchronizes with all previous or subsequent
 * calls to call_once with the same flag variable.
 * @param flag Pointer to an object that is used to ensure func is called only once
 * @param func The function to execute
 */
static inline void call_once(once_flag* flag, void (*func)(void)) {
    usched_call_once(flag, func);
}

/**
 * @brief Creates a new thread executing the function func. The function is invoked as func(arg).
 * If successful, the object pointed to by thr is set to the identifier of the new thread.
 * The completion of this function synchronizes-with the beginning of the thread.
 * @param thr
 * @param func
 * @param arg
 * @return
 */
static inline int thrd_create(thrd_t* thr, thrd_start_t func, void* arg) {
    *thr = usched_job_queue((usched_task_fn)func, arg);
    return __to_thrd_error(*thr != UUID_INVALID ? 0 : -1);
}

/**
 * @brief Checks whether lhs and rhs refer to the same thread.
 * @param a
 * @param b
 * @return
 */
static inline int thrd_equal(thrd_t a, thrd_t b) {
    return a == b;
}

/**
 * @brief Returns the identifier of the calling thread.
 * @return
 */
static inline thrd_t thrd_current(void) {
    return usched_job_current();
}

/**
 * @brief Provides a hint to the implementation to reschedule the execution of threads,
 * allowing other threads to run.
 */
static inline void thrd_yield(void) {
    usched_job_yield();
}

/**
 * @brief First, for every thread-specific storage key which was created with a non-null
 * destructor and for which the associated value is non-null (see tss_create), thrd_exit
 * sets the value associated with the key to NULL and then invokes the destructor with
 * the previous value of the key. The order in which the destructors are invoked is unspecified.
 * If, after this, there remain keys with both non-null destructors and values
 * (e.g. if a destructor executed tss_set), the process is repeated up to TSS_DTOR_ITERATIONS times.
 * Finally, the thrd_exit function terminates execution of the calling thread and sets its result code to res.
 * If the last thread in the program is terminated with thrd_exit, the entire program
 * terminates as if by calling exit with EXIT_SUCCESS as the argument (so the functions
 * registered by atexit are executed in the context of that last thread)
 * @param res
 */
static inline void thrd_exit(int res) {
    usched_job_exit(res);
}

/**
 * @brief Detaches the thread identified by thr from the current environment.
 * The resources held by the thread will be freed automatically once the thread exits.
 * @param thr
 * @return
 */
static inline int thrd_detach(thrd_t thr) {
    return __to_thrd_error(usched_job_detach(thr));
}

/**
 * @brief Blocks the current thread until the thread identified by thr finishes execution.
 * If res is not a null pointer, the result code of the thread is put to the location pointed to by res.
 * The termination of the thread synchronizes-with the completion of this function.
 * The behavior is undefined if the thread was previously detached or joined by another thread.
 * @param thr
 * @param res
 * @return
 */
static inline int thrd_join(thrd_t thr, int* res) {
    return __to_thrd_error(usched_job_join(thr, res));
}

/**
 * @brief Invokes a signal on the given thread id, for security reasons
 * it's only possible to signal threads local to the running process.
 * @param thr
 * @param sig
 * @return
 */
static inline int thrd_signal(thrd_t thr, int sig) {
    return __to_thrd_error(usched_job_signal(thr, sig));
}

/**
 * @brief Blocks the execution of the current thread for at least until the TIME_UTC
 * based time point pointed to by time_point has been reached.
 *
 * The sleep may resume earlier if a signal that is not ignored is received.
 * In such case, if remaining is not NULL, the remaining time duration is stored
 * into the object pointed to by remaining.
 *
 * @param[In]            duration Pointer to the duration that the thread should sleep for
 * @param[Out, Optional] remaining Pointer to the object to put the remaining time on interruption. May be NULL, in which case it is ignored
 * @return 0 on successful sleep, -1 if a signal occurred, other negative value if an error occurred.
 */
static inline int thrd_sleep(const struct timespec* duration, struct timespec* remaining) {
    struct timespec deadline;

    // Remaining is not supported in userspace threads, as all sleeps will be resolved
    // without any interruptions. Only blocking operations can be interrupted before their
    // timeout, and thus remaining will never be relevant.
    (void)remaining;

    // Sleep for userspace threads deal in absolute time points (UTC-based), and thus
    // we must convert it here. Luckily duration is expected to be in UTC as well, so we
    // can simply add it.
    timespec_get(&deadline, TIME_UTC);
    timespec_add(&deadline, duration, &deadline);
    return __to_thrd_error(usched_job_sleep(&deadline));
}

/**
 * @brief Creates a new mutex object with type. The object pointed to by mutex is set to an
 * identifier of the newly created mutex.
 * @param mutex
 * @param type
 * @return
 */
static inline int mtx_init(mtx_t* mutex, int type) {
    usched_mtx_init(mutex, type);
    return thrd_success;
}

/**
 * @brief Blocks the current thread until the mutex pointed to by mutex is locked.
 * The behavior is undefined if the current thread has already locked the mutex
 * and the mutex is not recursive.
 * @param mutex
 * @return
 */
static inline int mtx_lock(mtx_t* mutex) {
    usched_mtx_lock(mutex);
    return thrd_success;
}

/**
 * @brief Blocks the current thread until the mutex pointed to by mutex is
 * locked or until the TIME_UTC based time_point has been reached.
 * @param mutex
 * @param time_point
 * @return
 */
static inline int mtx_timedlock(mtx_t* restrict mutex, const struct timespec* restrict time_point) {
    return __to_thrd_error(usched_mtx_timedlock(mutex, time_point));
}

/**
 * @brief Tries to lock the mutex pointed to by mutex without blocking.
 * Returns immediately if the mutex is already locked.
 * @param mutex
 * @return
 */
static inline int mtx_trylock(mtx_t* mutex) {
    return __to_thrd_error(usched_mtx_trylock(mutex));
}

/**
 * @brief Unlocks the mutex pointed to by mutex.
 * @param mutex
 * @return
 */
static inline int mtx_unlock(mtx_t* mutex) {
    usched_mtx_unlock(mutex);
    return thrd_success;
}

/**
 * @brief Destroys the mutex pointed to by mutex. If there are threads waiting on mutex,
 * the behavior is undefined.
 * @param mutex
 */
static inline void mtx_destroy(mtx_t *mutex) {
    // not implemented
    (void)mutex;
}

/**
 * @brief Initializes new condition variable.
 * The object pointed to by cond will be set to value that identifies the condition variable.
 * @param cond
 * @return
 */
static inline int cnd_init(cnd_t* cond) {
    usched_cnd_init(cond);
    return thrd_success;
}

/**
 * @brief Unblocks one thread that currently waits on condition variable pointed to by cond.
 * If no threads are blocked, does nothing and returns thrd_success.
 * @param cond
 * @return
 */
static inline int cnd_signal(cnd_t* cond) {
    usched_cnd_notify_one(cond);
    return thrd_success;
}

/**
 * @brief Unblocks all thread that currently wait on condition variable pointed to by cond.
 * If no threads are blocked, does nothing and returns thrd_success.
 * @param cond
 * @return
 */
static inline int cnd_broadcast(cnd_t* cond) {
    usched_cnd_notify_all(cond);
    return thrd_success;
}

/**
 * @brief Atomically unlocks the mutex pointed to by mutex and blocks on the
 * condition variable pointed to by cond until the thread is signalled
 * by cnd_signal or cnd_broadcast. The mutex is locked again before the function returns.
 * @param cond
 * @param mutex
 * @return
 */
static inline int cnd_wait(cnd_t* cond, mtx_t* mutex) {
    usched_cnd_wait(cond, mutex);
    return thrd_success;
}

/**
 * @brief Atomically unlocks the mutex pointed to by mutex and blocks on the
 * condition variable pointed to by cond until the thread is signalled
 * by cnd_signal or cnd_broadcast, or until the TIME_UTC based time point
 * pointed to by time_point has been reached. The mutex is locked again
 * before the function returns.
 * @param cond
 * @param mutex
 * @param time_point
 * @return
 */
static inline int cnd_timedwait(cnd_t* restrict cond, mtx_t* restrict mutex, const struct timespec* restrict time_point) {
    return __to_thrd_error(
            usched_cnd_timedwait(cond, mutex, time_point)
    );
}

/**
 * @brief Destroys the condition variable pointed to by cond. If there are threads
 * waiting on cond, the behavior is undefined.
 * @param cond
 */
static inline void cnd_destroy(cnd_t* cond) {
    // not implemented
    (void)cond;
}

#else //!__OSCONFIG_GREEN_THREADS
#include <os/condition.h>
#include <os/mutex.h>
#include <os/once.h>
#include <os/threads.h>
#include <os/time.h>

typedef Condition_t cnd_t;
typedef Mutex_t mtx_t;
typedef OnceFlag_t once_flag;

enum {
    thrd_success    = OS_EOK,
    thrd_busy       = OS_EBUSY,
    thrd_timedout   = OS_ETIMEOUT,
    thrd_nomem      = OS_EOOM,
    thrd_error      = -1
};

enum {
    mtx_plain       = MUTEX_PLAIN,
    mtx_recursive   = MUTEX_RECURSIVE,
    mtx_timed       = MUTEX_TIMED
};

static inline int __to_thrd_error(oserr_t oserr) {
    switch (oserr) {
        case OS_EOK: return thrd_success;
        case OS_EBUSY: return thrd_busy;
        case OS_ETIMEOUT: return thrd_timedout;
        case OS_EOOM: return thrd_nomem;
        default: return thrd_error;
    }
}

/**
 * @brief Calls function func exactly once, even if invoked from several threads.
 * The completion of the function func synchronizes with all previous or subsequent
 * calls to call_once with the same flag variable.
 * @param flag Pointer to an object that is used to ensure func is called only once
 * @param func The function to execute
 */
static inline void call_once(once_flag* flag, void (*func)(void)) {
    CallOnce(flag, func);
}

/**
 * @brief Creates a new thread executing the function func. The function is invoked as func(arg).
 * If successful, the object pointed to by thr is set to the identifier of the new thread.
 * The completion of this function synchronizes-with the beginning of the thread.
 * @param thr
 * @param func
 * @param arg
 * @return
 */
static inline int thrd_create(thrd_t* thr, thrd_start_t func, void* arg) {
    ThreadParameters_t threadParameters;
    ThreadParametersInitialize(&threadParameters);
    return __to_thrd_error(
            ThreadsCreate(thr, &threadParameters, func, arg)
    );
}

/**
 * @brief Checks whether lhs and rhs refer to the same thread.
 * @param a
 * @param b
 * @return
 */
static inline int thrd_equal(thrd_t a, thrd_t b) {
    return a == b;
}

/**
 * @brief Returns the identifier of the calling thread.
 * @return
 */
static inline thrd_t thrd_current(void) {
    return ThreadsCurrentId();
}

/**
 * @brief Provides a hint to the implementation to reschedule the execution of threads,
 * allowing other threads to run.
 */
static inline void thrd_yield(void) {
    ThreadsYield();
}

/**
 * @brief First, for every thread-specific storage key which was created with a non-null
 * destructor and for which the associated value is non-null (see tss_create), thrd_exit
 * sets the value associated with the key to NULL and then invokes the destructor with
 * the previous value of the key. The order in which the destructors are invoked is unspecified.
 * If, after this, there remain keys with both non-null destructors and values
 * (e.g. if a destructor executed tss_set), the process is repeated up to TSS_DTOR_ITERATIONS times.
 * Finally, the thrd_exit function terminates execution of the calling thread and sets its result code to res.
 * If the last thread in the program is terminated with thrd_exit, the entire program
 * terminates as if by calling exit with EXIT_SUCCESS as the argument (so the functions
 * registered by atexit are executed in the context of that last thread)
 * @param res
 */
static inline void thrd_exit(int res) {
    ThreadsExit(res);
}

/**
 * @brief Detaches the thread identified by thr from the current environment.
 * The resources held by the thread will be freed automatically once the thread exits.
 * @param thr
 * @return
 */
static inline int thrd_detach(thrd_t thr) {
    return __to_thrd_error(ThreadsDetach(thr));
}

/**
 * @brief Blocks the current thread until the thread identified by thr finishes execution.
 * If res is not a null pointer, the result code of the thread is put to the location pointed to by res.
 * The termination of the thread synchronizes-with the completion of this function.
 * The behavior is undefined if the thread was previously detached or joined by another thread.
 * @param thr
 * @param res
 * @return
 */
static inline int thrd_join(thrd_t thr, int* res) {
    return __to_thrd_error(ThreadsJoin(thr, res));
}

/**
 * @brief Invokes a signal on the given thread id, for security reasons
 * it's only possible to signal threads local to the running process.
 * @param thr
 * @param sig
 * @return
 */
static inline int thrd_signal(thrd_t thr, int sig) {
    return __to_thrd_error(ThreadsSignal(thr, sig));
}

/**
 * @brief Blocks the execution of the current thread for at least until the TIME_UTC
 * based time point pointed to by time_point has been reached.
 *
 * The sleep may resume earlier if a signal that is not ignored is received.
 * In such case, if remaining is not NULL, the remaining time duration is stored
 * into the object pointed to by remaining.
 *
 * @param[In]            duration Pointer to the duration to sleep for
 * @param[Out, Optional] remaining Pointer to the object to put the remaining time on interruption. May be NULL, in which case it is ignored
 * @return 0 on successful sleep, -1 if a signal occurred, other negative value if an error occurred.
 */
static inline int thrd_sleep(const struct timespec* duration, struct timespec* remaining) {
    OSTimestamp_t utc, _remaining;
    int           error;

    // Sleep for threads deal in absolute time points (UTC-based), and thus
    // we must convert it here. Luckily duration is expected to be in UTC as well, so we
    // can simply add it.
    OSGetTime(OSTimeSource_UTC, &utc);
    OSTimestampAddTs(&utc, &utc, duration->tv_sec, duration->tv_nsec);
    error = __to_thrd_error(ThreadsSleep(&utc, &_remaining));
    if (error == thrd_timedout && remaining) {
        remaining->tv_sec = _remaining.Seconds;
        remaining->tv_nsec = _remaining.Nanoseconds;
    }
    return error;
}

/**
 * @brief Creates a new mutex object with type. The object pointed to by mutex is set to an
 * identifier of the newly created mutex.
 * @param mutex
 * @param type
 * @return
 */
static inline int mtx_init(mtx_t* mutex, int type) {
    return __to_thrd_error(MutexInitialize(mutex, type));
}

/**
 * @brief Blocks the current thread until the mutex pointed to by mutex is locked.
 * The behavior is undefined if the current thread has already locked the mutex
 * and the mutex is not recursive.
 * @param mutex
 * @return
 */
static inline int mtx_lock(mtx_t* mutex) {
    return __to_thrd_error(MutexLock(mutex));
}

/**
 * @brief Blocks the current thread until the mutex pointed to by mutex is
 * locked or until the TIME_UTC based time_point has been reached.
 * @param mutex
 * @param time_point
 * @return
 */
static inline int mtx_timedlock(mtx_t* restrict mutex, const struct timespec* restrict time_point) {
    return __to_thrd_error(MutexTimedLock(mutex, time_point));
}

/**
 * @brief Tries to lock the mutex pointed to by mutex without blocking.
 * Returns immediately if the mutex is already locked.
 * @param mutex
 * @return
 */
static inline int mtx_trylock(mtx_t* mutex) {
    return __to_thrd_error(MutexTryLock(mutex));
}

/**
 * @brief Unlocks the mutex pointed to by mutex.
 * @param mutex
 * @return
 */
static inline int mtx_unlock(mtx_t* mutex) {
    return __to_thrd_error(MutexUnlock(mutex));
}

/**
 * @brief Destroys the mutex pointed to by mutex. If there are threads waiting on mutex,
 * the behavior is undefined.
 * @param mutex
 */
static inline void mtx_destroy(mtx_t *mutex) {
    MutexDestroy(mutex);
}

/**
 * @brief Initializes new condition variable.
 * The object pointed to by cond will be set to value that identifies the condition variable.
 * @param cond
 * @return
 */
static inline int cnd_init(cnd_t* cond) {
    return __to_thrd_error(ConditionInitialize(cond));
}

/**
 * @brief Unblocks one thread that currently waits on condition variable pointed to by cond.
 * If no threads are blocked, does nothing and returns thrd_success.
 * @param cond
 * @return
 */
static inline int cnd_signal(cnd_t* cond) {
    return __to_thrd_error(ConditionSignal(cond));
}

/**
 * @brief Unblocks all thread that currently wait on condition variable pointed to by cond.
 * If no threads are blocked, does nothing and returns thrd_success.
 * @param cond
 * @return
 */
static inline int cnd_broadcast(cnd_t* cond) {
    return __to_thrd_error(ConditionBroadcast(cond));
}

/**
 * @brief Atomically unlocks the mutex pointed to by mutex and blocks on the
 * condition variable pointed to by cond until the thread is signalled
 * by cnd_signal or cnd_broadcast. The mutex is locked again before the function returns.
 * @param cond
 * @param mutex
 * @return
 */
static inline int cnd_wait(cnd_t* cond, mtx_t* mutex) {
    return __to_thrd_error(ConditionWait(cond, mutex, NULL));
}

/**
 * @brief Atomically unlocks the mutex pointed to by mutex and blocks on the
 * condition variable pointed to by cond until the thread is signalled
 * by cnd_signal or cnd_broadcast, or until the TIME_UTC based time point
 * pointed to by time_point has been reached. The mutex is locked again
 * before the function returns.
 * @param cond
 * @param mutex
 * @param time_point
 * @return
 */
static inline int cnd_timedwait(cnd_t* restrict cond, mtx_t* restrict mutex, const struct timespec* restrict time_point) {
    return __to_thrd_error(ConditionTimedWait(cond, mutex, time_point, NULL));
}

/**
 * @brief Destroys the condition variable pointed to by cond. If there are threads
 * waiting on cond, the behavior is undefined.
 * @param cond
 */
static inline void cnd_destroy(cnd_t* cond) {
    ConditionDestroy(cond);
}

#endif //__OSCONFIG_GREEN_THREADS

/**
 * @brief Creates new thread-specific storage key and stores it in the object pointed to by tss_key.
 * Although the same key value may be used by different threads,
 * the values bound to the key by tss_set are maintained on a per-thread
 * basis and persist for the life of the calling thread.
 * @param tssKey
 * @param destructor
 * @return
 */
CRTDECL(int,
tss_create(
    _In_ tss_t*     tssKey,
    _In_ tss_dtor_t destructor));

/**
 * @brief Returns the value held in thread-specific storage for the current thread
 * identified by tss_key. Different threads may get different values identified by the same key.
 * @param tssKey
 * @return
 */
CRTDECL(void*,
tss_get(
    _In_ tss_t tssKey));

/**
 * @brief Sets the value of the thread-specific storage identified by tss_id for the
 * current thread to val. Different threads may set different values to the same key.
 * @param tssKey
 * @param val
 * @return
 */
CRTDECL(int,
tss_set(
    _In_ tss_t tssKey,
    _In_ void* val));

/**
 * @brief Destroys the thread-specific storage identified by tss_id.
 * @param tssKey
 */
CRTDECL(void,
tss_delete(
    _In_ tss_t tssKey));

_CODE_END
#endif //!__STDC_THREADS__

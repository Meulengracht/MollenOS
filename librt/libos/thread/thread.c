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
#include <os/thread.h>
#include <os/syscall.h>

/* Includes
 * - Library */
#include <stddef.h>
#include <stdlib.h>
#include <signal.h>

/* Includes
 * - Compiler */
#if defined(_MSC_VER) && (_MSC_VER >= 1500)
#include <intrin.h>
#endif

/* ThreadPackage (Private)
 * Startup-package, used internally for starting threads */
typedef struct _ThreadPackage {
	ThreadFunc_t		 Entry;
	void				*Data;
} ThreadPackage_t;

/* _ThreadCRT
 * All new threads inherit this start function */
void _ThreadCRT(void *Data)
{
	// Variables (and TLS)
	ThreadLocalStorage_t Tls;
	ThreadPackage_t *Tp;
	int RetVal = 0;

	// Initialize TLS and package pointer
	TLSInitInstance(&Tls);
	Tp = (ThreadPackage_t*)Data;

	// Call the thread entry
	RetVal = Tp->Entry(Tp->Data);

	// Cleanup and call threadexit
	TLSDestroyInstance(&Tls);
	free(Tp);
	ThreadExit(RetVal);
}

/* ThreadOnce
 * Executes the ThreadOnceFunc_t object exactly once, even if 
 * called from several threads. */
void 
ThreadOnce(
	_In_ ThreadOnce_t *Control,
	_In_ ThreadOnceFunc_t Function)
{
	// Use interlocked exchange for this operation
#if defined(_MSC_VER) && (_MSC_VER >= 1500)
	long RunOnce = _InterlockedExchange(Control, 0);
#else
#error "Implement ThreadOnce support for given compiler"
#endif

	// Sanity, RunOnce is 1 if first time
	if (RunOnce != 0) {
		Function();
	}
}

/* ThreadCreate
 * Creates a new thread executing the given function and with the
 * given arguments. The id of the new thread is returned. */
UUId_t 
ThreadCreate(
	_In_ ThreadFunc_t Entry, 
	_In_Opt_ void *Data)
{
	// Variables
	ThreadPackage_t *Tp = NULL;

	// Allocate a new startup-package
	Tp = (ThreadPackage_t*)malloc(sizeof(ThreadPackage_t));
	Tp->Entry = Entry;
	Tp->Data = Data;

	// Redirect to operating system to handle rest
	return (UUId_t)Syscall3(SYSCALL_THREADCREATE,
		SYSCALL_PARAM((ThreadFunc_t)_ThreadCRT),
		SYSCALL_PARAM(Tp), SYSCALL_PARAM(0));
}

/* ThreadExit
 * Signals to the operating system that the caller thread is now
 * done and can be cleaned up. This does not terminate the process
 * unless it's the last thread alive */
OsStatus_t
ThreadExit(
	_In_ int ExitCode)
{
	// Perform cleanup of TLS
	TLSCleanup(ThreadGetId());
	TLSDestroyInstance(TLSGetCurrent());

	// Redirect to os-function, there is no return
	Syscall1(SYSCALL_THREADEXIT, SYSCALL_PARAM(ExitCode));
	return OsSuccess;
}

/* ThreadJoin
 * Waits for the given thread to finish executing. The returned value
 * is either the return-code from the running thread or -1 in case of
 * invalid thread-id. */
int 
ThreadJoin(
	_In_ UUId_t ThreadId)
{
	// The syscall actually does most of
	// the validation for us, returns -1 on error
	return Syscall1(SYSCALL_THREADJOIN, SYSCALL_PARAM(ThreadId));
}

/* ThreadSignal
 * Invokes a signal on the given thread id, for security reasons
 * it's only possible to signal threads local to the running process. */
OsStatus_t
ThreadSignal(
	_In_ UUId_t ThreadId,
	_In_ int SignalCode)
{
	// Simply redirect
	return (OsStatus_t)Syscall2(SYSCALL_THREADSIGNAL,
		SYSCALL_PARAM(ThreadId), SYSCALL_PARAM(SignalCode));
}

/* ThreadKill
 * Kill's the given thread by sending a SIGKILL to the thread, forcing
 * it to run cleanup and terminate. Thread might not terminate immediately. */
OsStatus_t 
ThreadKill(
	_In_ UUId_t ThreadId)
{
	// Simply signal the thread to kill itself
	return (OsStatus_t)Syscall2(SYSCALL_THREADSIGNAL,
		SYSCALL_PARAM(ThreadId), SYSCALL_PARAM(SIGKILL));
}

/* ThreadSleep
 * Sleeps the current thread for the given milliseconds. */
void 
ThreadSleep(
	_In_ size_t MilliSeconds)
{
	// Sanitize just in case - to save a syscall
	if (MilliSeconds == 0) {
		return;
	}

	// Redirect the call
	Syscall1(SYSCALL_THREADSLEEP, SYSCALL_PARAM(MilliSeconds));
}

/* ThreadGetId
 * Retrieves the thread id of the calling thread. */
UUId_t
ThreadGetId(void)
{
	// If it's already cached, use that
	if (TLSGetCurrent()->Id != UUID_INVALID) {
		return TLSGetCurrent()->Id;
	}

	// Otherwise invoke OS to refresh id
	TLSGetCurrent()->Id = (UUId_t)Syscall0(SYSCALL_THREADID);
	return TLSGetCurrent()->Id;
}

/* ThreadYield
 * This yields the current thread 
 * and gives cpu time to another thread */
void 
ThreadYield(void)
{
	// Nothing to validate, just call
	Syscall0(SYSCALL_THREADYIELD);
}

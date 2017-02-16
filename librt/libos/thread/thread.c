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

/* Includes
 * - Compiler */
#if defined(_MSC_VER) && (_MSC_VER >= 1500)
#include <intrin.h>
#endif

/* Private thread initializor 
 * package, used internally for starting threads */
typedef struct _ThreadPackage {
	ThreadFunc_t		 Entry;
	void				*Data;
} ThreadPackage_t;

/* _ThreadCRT
 * All new threads inherit this start function */
void _ThreadCRT(void *Data)
{
	/* Allocate TSS */
	ThreadLocalStorage_t Tls;
	ThreadPackage_t *Tp;
	int RetVal = 0;

	/* Initialize the TLS */
	TLSInitInstance(&Tls);
	Tp = (ThreadPackage_t*)Data;

	/* Run entry */
	RetVal = Tp->Entry(Tp->Data);

	/* Cleanup */
	TLSDestroyInstance(&Tls);
	free(Tp);
	ThreadExit(RetVal);
}

/* ThreadOnce
 * This is a support thread function
 * that makes sure that even with shared
 * functions between threads a function
 * only ever gets called once */
void 
ThreadOnce(
	_In_ ThreadOnce_t *Control,
	_In_ ThreadOnceFunc_t Function)
{
	/* Use interlocked exchange 
	 * for this operation */
	long RunOnce = _InterlockedExchange(Control, 0);

	/* Sanity, RunOnce is 1 
	 * if first time */
	if (RunOnce != 0) {
		Function();
	}
}

/* ThreadCreate
 * Creates a new thread bound to 
 * the calling process, with the given
 * entry point and arguments */
UUId_t 
ThreadCreate(
	_In_ ThreadFunc_t Entry, 
	_In_Opt_ void *Data)
{
	/* Allocate thread data */
	ThreadPackage_t *Tp = (ThreadPackage_t*)malloc(sizeof(ThreadPackage_t));

	/* Set data */
	Tp->Entry = Entry;
	Tp->Data = Data;

	/* This is just a redirected syscall */
	return (UUId_t)Syscall3(SYSCALL_THREADID, SYSCALL_PARAM((ThreadFunc_t)_ThreadCRT),
		SYSCALL_PARAM(Tp), SYSCALL_PARAM(0));
}

/* ThreadExit
 * Exits the current thread and 
 * instantly yields control to scheduler */
void 
ThreadExit(
	_In_ int ExitCode)
{
	/* Cleanup TLS */
	TLSCleanup(ThreadGetCurrentId());
	TLSDestroyInstance(TLSGetCurrent());

	/* The syscall actually does most of
	 * the validation for us */
	Syscall1(SYSCALL_THREADKILL, SYSCALL_PARAM(ExitCode));
}

/* ThreadJoin
 * waits for a given thread to finish executing, and
 * returns it's exit code, Must be in same
 * process as asking thread */
int 
ThreadJoin(
	_In_ UUId_t ThreadId)
{
	/* The syscall actually does most of
	 * the validation for us, returns -1 on err */
	return Syscall1(SYSCALL_THREADJOIN, SYSCALL_PARAM(ThreadId));
}

/* ThreadKill
 * Thread kill, kills the given thread
 * id, must belong to same process as the
 * thread that asks. */
OsStatus_t 
ThreadKill(
	_In_ UUId_t ThreadId)
{
	/* The syscall actually does most of 
	 * the validation for us, this returns
	 * 0 if everything went ok */
	return Syscall1(SYSCALL_THREADKILL, SYSCALL_PARAM(ThreadId));
}

/* ThreadSleep
 * Sleeps the current thread for the
 * given milliseconds. */
void 
ThreadSleep(
	_In_ size_t MilliSeconds)
{
	/* This is also just a redirected syscall
	 * we don't validate the asked time, it's 
	 * up to the user not to fuck it up */
	if (MilliSeconds == 0) {
		return;
	}

	/* Gogo! */
	Syscall1(SYSCALL_THREADSLEEP, SYSCALL_PARAM(MilliSeconds));
}

/* ThreadGetCurrentId
 * Retrieves the current thread id */
UUId_t 
ThreadGetCurrentId(void)
{
	/* We save this in the reserved
	 * space to speed up this call */
	if (TLSGetCurrent()->Id != 0) {
		return TLSGetCurrent()->Id;
	}

	/* This is just a redirected syscall
	 * no arguments involved, no validation */
	TLSGetCurrent()->Id = (UUId_t)Syscall0(SYSCALL_THREADID);

	/* Done! */
	return TLSGetCurrent()->Id;
}

/* ThreadYield
 * This yields the current thread 
 * and gives cpu time to another thread */
void 
ThreadYield(void)
{
	/* This is just a redirected syscall 
     * no arguments involved, no validation */
	Syscall0(SYSCALL_THREADYIELD);
}

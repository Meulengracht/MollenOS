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
* MollenOS - Threading Functions
*/

/* Includes */
#include <os/MollenOS.h>
#include <os/Syscall.h>
#include <os/Thread.h>

/* C Library */
#include <crtdefs.h>
#include <stddef.h>
#include <stdlib.h>

#ifdef LIBC_KERNEL
void __ThreadLibCEmpty(void)
{
}
#else

/* Private Includes */
#if defined(_MSC_VER) && (_MSC_VER >= 1500)
#include <intrin.h>
#endif

/* Private Definitions */
#ifdef _X86_32
#define MOLLENOS_RESERVED_SPACE	0xFFFFFFF4
#elif defined(X86_64)
#define MOLLENOS_RESERVED_SPACE	0xFFFFFFF4
#endif

/* Structure (private) */
typedef struct _ThreadPackage
{
	/* Entry point of 
	 * the thread */
	ThreadFunc_t Entry;

	/* User-defined data
	 * for the thread */
	void *Data;

} ThreadPackage_t;

/* Thread CRT Entry Point */
void _ThreadCRT(void *Data)
{
	/* Allocate TSS */
	uint64_t *ReservedSpace;
	ThreadPackage_t *Tp;
	int RetVal = 0;
	//_locale_tstruct tminfo;

	/* Initialize the 8 bytes 
	 * of storage */
	ReservedSpace = (uint64_t*)MOLLENOS_RESERVED_SPACE;
	*ReservedSpace = 0;

	/* Cast */
	Tp = (ThreadPackage_t*)Data;

	/* Run entry */
	RetVal = Tp->Entry(Tp->Data);

	/* Cleanup */
	free(Tp);

	/* Exit Thread */
	ThreadExit(RetVal);
}

/* This is a support thread function
 * that makes sure that even with shared
 * functions between threads a function
 * only ever gets called once */
void ThreadOnce(ThreadOnce_t *Control, ThreadOnceFunc_t Function)
{
	/* Use interlocked exchange 
	 * for this operation */
	long RunOnce = _InterlockedExchange(Control, 0);

	/* Sanity, RunOnce is 1 
	 * if first time */
	if (RunOnce != 0)
		Function();
}

/* Creates a new thread bound to
 * the calling process, with the given
 * entry point and arguments */
TId_t ThreadCreate(ThreadFunc_t Entry, void *Data)
{
	/* Allocate thread data */
	ThreadPackage_t *Tp = (ThreadPackage_t*)malloc(sizeof(ThreadPackage_t));

	/* Set data */
	Tp->Entry = Entry;
	Tp->Data = Data;

	/* This is just a redirected syscall */
	return (TId_t)Syscall3(MOLLENOS_SYSCALL_THREADID, MOLLENOS_SYSCALL_PARAM((ThreadFunc_t)_ThreadCRT),
		MOLLENOS_SYSCALL_PARAM(Tp), MOLLENOS_SYSCALL_PARAM(0));
}

/* Exits the current thread and
 * instantly yields control to scheduler */
void ThreadExit(int ExitCode)
{
	/* Cleanup TLS */
	TLSCleanup(ThreadGetCurrentId());
	TLSUnregister(ThreadGetCurrentId());

	/* The syscall actually does most of
	 * the validation for us */
	Syscall1(MOLLENOS_SYSCALL_THREADKILL, MOLLENOS_SYSCALL_PARAM(ExitCode));
}

/* Thread join, waits for a given
 * thread to finish executing, and
 * returns it's exit code, works
 * like processjoin. Must be in same
 * process as asking thread */
int ThreadJoin(TId_t ThreadId)
{
	/* The syscall actually does most of
	 * the validation for us, returns -1 on err */
	return Syscall1(MOLLENOS_SYSCALL_THREADJOIN, MOLLENOS_SYSCALL_PARAM(ThreadId));
}

/* Thread kill, kills the given thread
 * id, must belong to same process as the
 * thread that asks. */
int ThreadKill(TId_t ThreadId)
{
	/* The syscall actually does most of 
	 * the validation for us, this returns
	 * 0 if everything went ok */
	return Syscall1(MOLLENOS_SYSCALL_THREADKILL, MOLLENOS_SYSCALL_PARAM(ThreadId));
}

/* Thread sleep,
 * Sleeps the current thread for the
 * given milliseconds. */
void ThreadSleep(size_t MilliSeconds)
{
	/* This is also just a redirected syscall
	 * we don't validate the asked time, it's 
	 * up to the user not to fuck it up */
	if (MilliSeconds == 0)
		return;

	/* Gogo! */
	Syscall1(MOLLENOS_SYSCALL_THREADSLEEP, MOLLENOS_SYSCALL_PARAM(MilliSeconds));
}

/* Thread get current id
 * Get's the current thread id */
TId_t ThreadGetCurrentId(void)
{
	/* Variables */
	TId_t *tPtr = (TId_t*)MOLLENOS_RESERVED_SPACE;

	/* We save this in the reserved
	 * space to speed up this call */
	if (*tPtr != 0) {
		return *tPtr;
	}

	/* This is just a redirected syscall
	 * no arguments involved, no validation */
	*tPtr = (TId_t)Syscall0(MOLLENOS_SYSCALL_THREADID);

	/* Done! */
	return *tPtr;
}

/* This yields the current thread
 * and gives cpu time to another thread */
void ThreadYield(void)
{
	/* This is just a redirected syscall 
     * no arguments involved, no validation */
	Syscall0(MOLLENOS_SYSCALL_THREADYIELD);
}

#endif
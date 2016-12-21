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
 * MollenOS - Miscalleous Functions
 */

/* Includes */
#include <os/mollenos.h>
#include <os/syscall.h>
#include <os/thread.h>
#include <signal.h>
#include <string.h>
#include <stdio.h>

/* Private Definitions */
#ifdef _X86_32
#define MOLLENOS_RESERVED_SPACE	0xFFFFFFF4
#elif defined(X86_64)
#define MOLLENOS_RESERVED_SPACE	0xFFFFFFF4
#endif

/* Don't define any of this
 * if the static version of the library is
 * being build! */
#ifndef _CRTIMP_STATIC

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
	Syscall1(SYSCALL_THREADSLEEP, SYSCALL_PARAM(MilliSeconds));
}

/* TLSGetCurrent 
 * Retrieves the local storage space
 * for the current thread */
ThreadLocalStorage_t *TLSGetCurrent(void)
{
	/* Dereference the pointer */
	size_t Address = *((size_t*)MOLLENOS_RESERVED_SPACE);

	/* Done */
	return (ThreadLocalStorage_t*)Address;
}

/* Query */
int MollenOSDeviceQuery(OSDeviceType_t Type, int Request, void *Buffer, size_t Length)
{
	/* Not used atm */
	_CRT_UNUSED(Request);

	/* Prep for syscall */
	return Syscall3(SYSCALL_DEVQUERY, SYSCALL_PARAM(Type), 
		SYSCALL_PARAM(Buffer), SYSCALL_PARAM(Length));
}

/* Const Message */
const char *__SysTypeMessage = "CLIB";

/* Write to sysout */
void MollenOSSystemLog(const char *Format, ...)
{
	/* We need a static, temporary buffer */
	va_list Args;
	char TmpBuffer[256];

	/* Reset the buffer */
	memset(&TmpBuffer[0], 0, sizeof(TmpBuffer));

	/* Now use that one to format the string
	 * in using sprintf */
	va_start(Args, Format);
	vsprintf(&TmpBuffer[0], Format, Args);
	va_end(Args);

	/* Now spit it out */
	Syscall2(0, SYSCALL_PARAM(__SysTypeMessage), 
		SYSCALL_PARAM(&TmpBuffer[0]));
}

/* IPC - Read
 * This returns -1 if something went wrong reading
 * a message from the message queue, otherwise it returns 0
 * and fills the structures with information about the message */
int PipeRead(int Pipe, void *Buffer, size_t Length)
{
	/* Variables */
	int Result = 0;

	/* Read is rather just calling the underlying syscall */
	Result = Syscall4(SYSCALL_READPIPE, SYSCALL_PARAM(Pipe),
		SYSCALL_PARAM(Buffer), SYSCALL_PARAM(Length), 0);

	/* Sanitize the return parameters */
	if (Result < 0) {
		raise(SIGPIPE);
	}

	/* Done! */
	return Result;
}

#endif

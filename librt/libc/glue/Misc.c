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
ThreadLocalStorage_t *
TLSGetCurrent(void)
{
	return (ThreadLocalStorage_t*)__get_reserved(0);
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

#endif

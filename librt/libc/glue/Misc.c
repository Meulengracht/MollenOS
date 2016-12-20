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
#include <os/MollenOS.h>
#include <os/Syscall.h>
#include <os/Thread.h>
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
	Syscall1(MOLLENOS_SYSCALL_THREADSLEEP, MOLLENOS_SYSCALL_PARAM(MilliSeconds));
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
int MollenOSDeviceQuery(MollenOSDeviceType_t Type, int Request, void *Buffer, size_t Length)
{
	/* Not used atm */
	_CRT_UNUSED(Request);

	/* Prep for syscall */
	return Syscall3(MOLLENOS_SYSCALL_DEVQUERY, MOLLENOS_SYSCALL_PARAM(Type), 
		MOLLENOS_SYSCALL_PARAM(Buffer), MOLLENOS_SYSCALL_PARAM(Length));
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
	Syscall2(0, MOLLENOS_SYSCALL_PARAM(__SysTypeMessage), 
		MOLLENOS_SYSCALL_PARAM(&TmpBuffer[0]));
}

/* IPC - Read/Wait - BLOCKING OP
 * This returns -2/-1 if something went wrong reading
 * a message from the message queue, otherwise it returns 0
 * and fills the structures with information about
 * the message */
int MollenOSMessageWait(MEventMessage_t *Message)
{
	/* Cast to an address pointer */
	uint8_t *MsgPointer = (uint8_t*)Message;

	/* Syscall! */
	return Syscall1(MOLLENOS_SYSCALL_READMSG, MOLLENOS_SYSCALL_PARAM(MsgPointer));
}

#endif

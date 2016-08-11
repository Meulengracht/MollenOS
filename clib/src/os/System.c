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
* MollenOS System Interface
*/

/* Includes */
#include <os/MollenOS.h>
#include <os/Syscall.h>
#include <os/Ipc.h>

/* C-Library */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#ifdef LIBC_KERNEL
void __SystemLibCEmpty(void)
{
}
#else

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

/* End Boot Sequence */
int MollenOSEndBoot(void)
{
	/* Prep for syscall */
	return Syscall0(MOLLENOS_SYSCALL_ENDBOOT);
}

/* Register Event Target */
int MollenOSRegisterWM(void)
{
	/* Prep for syscall */
	return Syscall0(MOLLENOS_SYSCALL_REGWM);
}

/* This function returns screen geomemtry
 * descriped as a rectangle structure, which
 * must be pre-allocated */
void MollenOSGetScreenGeometry(Rect_t *Rectangle)
{
	/* Vars */
	MollenOSVideoDescriptor_t VidDescriptor;

	/* Do it */
	MollenOSDeviceQuery(DeviceVideo, 0, &VidDescriptor, sizeof(MollenOSVideoDescriptor_t));

	/* Save info */
	Rectangle->x = 0;
	Rectangle->y = 0;
	Rectangle->w = VidDescriptor.Width;
	Rectangle->h = VidDescriptor.Height;
}

/* IPC - Peek - NO BLOCK
 * This returns -1 if there is no new messages
 * in the message-queue, otherwise it returns 0 
 * and fills the base structures with information about
 * the message */
int MollenOSMessagePeek(MEventMessage_t *Message)
{
	/* Variables */
	int RetVal = 0;

	/* Syscall! */
	RetVal = Syscall2(MOLLENOS_SYSCALL_PEEKMSG, MOLLENOS_SYSCALL_PARAM(Message),
		MOLLENOS_SYSCALL_PARAM(sizeof(MEventMessageBase_t)));

	/* Sanity */
	if (RetVal > 0)
		return 0;
	else
		return -1;
}

/* IPC - Read/Wait - BLOCKING OP
 * This returns -2/-1/0 if something went wrong reading
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

/* IPC - Write
 * Returns -1 if message failed to send 
 * Returns -2 if message-target didn't exist
 * Returns 0 if message was sent correctly to target */
int MollenOSMessageSend(IpcComm_t Target, void *Message, size_t MessageLength)
{
	/* Variables */
	int RetVal = 0;

	/* Syscall! */
	RetVal = Syscall3(MOLLENOS_SYSCALL_WRITEMSG, MOLLENOS_SYSCALL_PARAM(Target),
		MOLLENOS_SYSCALL_PARAM(Message), MOLLENOS_SYSCALL_PARAM(MessageLength));

	/* Sanity */
	if (RetVal > 0)
		return 0;

	/* Done! */
	return RetVal;
}

/* IPC - Sleep 
 * This suspends the current process-thread
 * and puts it in a sleep state untill either
 * the timeout runs out or it recieves a wake 
 * signal. */
int MollenOSSignalWait(size_t Timeout)
{
	/* Variables */
	int RetVal = 0;

	/* Syscall! */
	RetVal = Syscall1(MOLLENOS_SYSCALL_PSIGWAIT, MOLLENOS_SYSCALL_PARAM(Timeout));

	/* Done! */
	return RetVal;
}

/* IPC - Wake 
 * This wakes up a thread in suspend mode on the 
 * target. This should be used in conjunction with
 * the Sleep. */
int MollenOSSignalWake(IpcComm_t Target)
{
	/* Variables */
	int RetVal = 0;

	/* Syscall! */
	RetVal = Syscall1(MOLLENOS_SYSCALL_PSIGSEND, MOLLENOS_SYSCALL_PARAM(Target));

	/* Done! */
	return RetVal;
}

/* Memory - Share
 * This shares a piece of memory with the
 * target process. The function returns NULL
 * on failure to share the piece of memory
 * otherwise it returns the new buffer handle
 * that can be accessed by the other process */
void *MollenOSMemoryShare(IpcComm_t Target, void *Buffer, size_t Size)
{
	/* Variables */
	int RetVal = 0;

	/* Syscall! */
	RetVal = Syscall3(MOLLENOS_SYSCALL_MEMSHARE, MOLLENOS_SYSCALL_PARAM(Target),
		MOLLENOS_SYSCALL_PARAM(Buffer), MOLLENOS_SYSCALL_PARAM(Size));

	/* Done! */
	return (void*)RetVal;
}

/* Memory - Unshare
 * This takes a previous shared memory handle
 * and unshares it again from the target process
 * The function returns 0 if unshare was succesful,
 * otherwise -1 */
int MollenOSMemoryUnshare(IpcComm_t Target, void *MemoryHandle, size_t Size)
{
	/* Variables */
	int RetVal = 0;

	/* Syscall! */
	RetVal = Syscall3(MOLLENOS_SYSCALL_MEMUNSHARE, MOLLENOS_SYSCALL_PARAM(Target),
		MOLLENOS_SYSCALL_PARAM(MemoryHandle), MOLLENOS_SYSCALL_PARAM(Size));

	/* Done! */
	return RetVal;
}

#endif
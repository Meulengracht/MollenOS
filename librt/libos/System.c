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

/* Includes
 * - C-Library */
#include <signal.h>
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
	return Syscall0(SYSCALL_ENDBOOT);
}

/* Register Event Target */
int MollenOSRegisterWM(void)
{
	/* Prep for syscall */
	return Syscall0(SYSCALL_REGWM);
}

/* This function returns screen geomemtry
 * descriped as a rectangle structure, which
 * must be pre-allocated */
void MollenOSGetScreenGeometry(Rect_t *Rectangle)
{
	/* Vars */
	OSVideoDescriptor_t VidDescriptor;

	/* Do it */
	MollenOSDeviceQuery(DeviceVideo, 0, &VidDescriptor, sizeof(OSVideoDescriptor_t));

	/* Save info */
	Rectangle->x = 0;
	Rectangle->y = 0;
	Rectangle->w = VidDescriptor.Width;
	Rectangle->h = VidDescriptor.Height;
}

/* IPC - Open - Pipe
 * Opens a new communication pipe on the given
 * port for this process, if one already exists
 * SIGPIPE is signaled */
int PipeOpen(int Port)
{

}

/* IPC - Close - Pipe
* Closes an existing communication pipe on the given
* port for this process, if one doesn't exists
* SIGPIPE is signaled */
int PipeClose(int Port)
{

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

/* IPC - Send
 * Returns -1 if message failed to send
 * Returns -2 if message-target didn't exist
 * Returns 0 if message was sent correctly to target */
int PipeSend(IpcComm_t Target, int Port, void *Message, size_t Length)
{
	/* Variables */
	int Result = 0;

	/* Read is rather just calling the underlying syscall */
	Result = Syscall4(SYSCALL_WRITEPIPE, SYSCALL_PARAM(Target),
		SYSCALL_PARAM(Port), SYSCALL_PARAM(Message), SYSCALL_PARAM(Length));

	/* Sanitize the return parameters */
	if (Result < 0) {
		raise(SIGPIPE);
	}

	/* Done! */
	return Result;
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
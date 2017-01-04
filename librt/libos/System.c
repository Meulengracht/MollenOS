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
#include <os/mollenos.h>
#include <os/syscall.h>

/* Includes
 * - C-Library */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

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
	RetVal = Syscall3(SYSCALL_MEMSHARE, SYSCALL_PARAM(Target),
		SYSCALL_PARAM(Buffer), SYSCALL_PARAM(Size));

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
	RetVal = Syscall3(SYSCALL_MEMUNSHARE, SYSCALL_PARAM(Target),
		SYSCALL_PARAM(MemoryHandle), SYSCALL_PARAM(Size));

	/* Done! */
	return RetVal;
}

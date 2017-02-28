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
void MollenOSSystemLog(__CONST char *Format, ...)
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
void MollenOSEndBoot(void)
{
	/* Prep for syscall */
	Syscall0(SYSCALL_ENDBOOT);
}

/* ScreenQueryGeometry
 * This function returns screen geomemtry
 * descriped as a rectangle structure */
OsStatus_t 
ScreenQueryGeometry(
	_Out_ Rect_t *Rectangle)
{
	/* Vars */
	OSVideoDescriptor_t VidDescriptor = { 0 };

	/* Do it */
	// TODO
	//MollenOSDeviceQuery(DeviceVideo, 0, &VidDescriptor, sizeof(OSVideoDescriptor_t));

	/* Save info */
	Rectangle->x = 0;
	Rectangle->y = 0;
	Rectangle->w = VidDescriptor.Width;
	Rectangle->h = VidDescriptor.Height;

	return OsNoError;
}

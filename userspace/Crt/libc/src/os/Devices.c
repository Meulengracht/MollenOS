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
* MollenOS Device Interface
*/

/* Includes */
#include <os/MollenOS.h>
#include <os/Syscall.h>

#ifdef LIBC_KERNEL
void __DeviceLibCEmpty(void)
{
}
#else

/* Query */
int MollenOSDeviceQuery(MollenOSDeviceType_t Type, int Request, void *Buffer, size_t Length)
{
	/* Not used atm */
	_CRT_UNUSED(Request);

	/* Prep for syscall */
	return Syscall3(MOLLENOS_SYSCALL_DEVQUERY, MOLLENOS_SYSCALL_PARAM(Type), 
		MOLLENOS_SYSCALL_PARAM(Buffer), MOLLENOS_SYSCALL_PARAM(Length));
}

#endif
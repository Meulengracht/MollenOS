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

#ifdef LIBC_KERNEL
void __SystemLibCEmpty(void)
{
}
#else

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

#endif
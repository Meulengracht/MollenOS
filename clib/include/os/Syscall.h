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
* MollenOS Syscall Interface
*/

/* Guard */
#ifndef _MOLLENOS_SYSCALLS_H_
#define _MOLLENOS_SYSCALLS_H_

/* Includes */
#include <crtdefs.h>

/* Definitions */
#define MOLLENOS_SYSCALL_PARAM(Arg)			((int)Arg)

#define MOLLENOS_SYSCALL_TERMINATE			0x1
#define MOLLENOS_SYSCALL_YIELD				0x2
#define MOLLENOS_SYSCALL_PROCSPAWN			0x3
#define MOLLENOS_SYSCALL_PROCJOIN			0x4
#define MOLLENOS_SYSCALL_PROCKILL			0x5

#define MOLLENOS_SYSCALL_LOADSO				0x8
#define MOLLENOS_SYSCALL_LOADPROC			0x9
#define MOLLENOS_SYSCALL_UNLOADSO			0xA

#define MOLLENOS_SYSCALL_MEMALLOC			0x15
#define MOLLENOS_SYSCALL_MEMFREE			0x16

#define MOLLENOS_SYSCALL_READMSG			0x1F
#define MOLLENOS_SYSCALL_WRITEMSG			0x20
#define MOLLENOS_SYSCALL_PEEKMSG			0x21

#define MOLLENOS_SYSCALL_VFSOPEN			0x29
#define MOLLENOS_SYSCALL_VFSCLOSE			0x2A
#define MOLLENOS_SYSCALL_VFSREAD			0x2B
#define MOLLENOS_SYSCALL_VFSWRITE			0x2C
#define MOLLENOS_SYSCALL_VFSSEEK			0x2D
#define MOLLENOS_SYSCALL_VFSFLUSH			0x2E
#define MOLLENOS_SYSCALL_VFSDELETE			0x2F
#define MOLLENOS_SYSCALL_VFSMOVE			0x30
#define MOLLENOS_SYSCALL_VFSQUERY			0x31
#define MOLLENOS_SYSCALL_VFSPATH			0x32

#define MOLLENOS_SYSCALL_DEVQUERY			0x47

#define MOLLENOS_SYSCALL_ENDBOOT			0x51
#define MOLLENOS_SYSCALL_REGWM				0x52

/* Prototypes */
_CRT_EXTERN int Syscall0(int Function);
_CRT_EXTERN int Syscall1(int Function, int Arg0);
_CRT_EXTERN int Syscall2(int Function, int Arg0, int Arg1);
_CRT_EXTERN int Syscall3(int Function, int Arg0, int Arg1, int Arg2);
_CRT_EXTERN int Syscall4(int Function, int Arg0, int Arg1, int Arg2, int Arg3);
_CRT_EXTERN int Syscall5(int Function, int Arg0, int Arg1, int Arg2, int Arg3, int Arg4);

#endif //!_MOLLENOS_SYSCALLS_H_
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

/* Process System Calls */
#define MOLLENOS_SYSCALL_TERMINATE			0x1
#define MOLLENOS_SYSCALL_PROCQUERY			0x2
#define MOLLENOS_SYSCALL_PROCSPAWN			0x3
#define MOLLENOS_SYSCALL_PROCJOIN			0x4
#define MOLLENOS_SYSCALL_PROCKILL			0x5
#define MOLLENOS_SYSCALL_PROCSIGNAL			0x6
#define MOLLENOS_SYSCALL_PROCRAISE			0x7

/* Shared Object System Calls */
#define MOLLENOS_SYSCALL_LOADSO				0x8
#define MOLLENOS_SYSCALL_LOADPROC			0x9
#define MOLLENOS_SYSCALL_UNLOADSO			0xA

/* Threading System Calls */
#define MOLLENOS_SYSCALL_THREADCREATE		0xB
#define MOLLENOS_SYSCALL_THREADEXIT			0xC
#define MOLLENOS_SYSCALL_THREADKILL			0xD
#define MOLLENOS_SYSCALL_THREADJOIN			0xE
#define MOLLENOS_SYSCALL_THREADSLEEP		0xF
#define MOLLENOS_SYSCALL_THREADYIELD		0x10
#define MOLLENOS_SYSCALL_THREADID			0x11

/* Synchronization System Calls */
#define MOLLENOS_SYSCALL_CONDCREATE			0x15
#define MOLLENOS_SYSCALL_CONDDESTROY		0x16
#define MOLLENOS_SYSCALL_SYNCSLEEP			0x17
#define MOLLENOS_SYSCALL_SYNCWAKEONE		0x18
#define MOLLENOS_SYSCALL_SYNCWAKEALL		0x19

/* Memory System Calls */
#define MOLLENOS_SYSCALL_MEMALLOC			0x1F
#define MOLLENOS_SYSCALL_MEMFREE			0x20
#define MOLLENOS_SYSCALL_MEMQUERY			0x21
#define MOLLENOS_SYSCALL_MEMSHARE			0x22
#define MOLLENOS_SYSCALL_MEMUNSHARE			0x23

/* IPC System Calls */
#define MOLLENOS_SYSCALL_PEEKMSG			0x29
#define MOLLENOS_SYSCALL_READMSG			0x2A
#define MOLLENOS_SYSCALL_WRITEMSG			0x2B
#define MOLLENOS_SYSCALL_PSIGWAIT			0x2C
#define MOLLENOS_SYSCALL_PSIGSEND			0x2D

/* VFS System Calls */
#define MOLLENOS_SYSCALL_VFSOPEN			0x33
#define MOLLENOS_SYSCALL_VFSCLOSE			0x34
#define MOLLENOS_SYSCALL_VFSREAD			0x35
#define MOLLENOS_SYSCALL_VFSWRITE			0x36
#define MOLLENOS_SYSCALL_VFSSEEK			0x37
#define MOLLENOS_SYSCALL_VFSFLUSH			0x38
#define MOLLENOS_SYSCALL_VFSDELETE			0x39
#define MOLLENOS_SYSCALL_VFSMOVE			0x3A
#define MOLLENOS_SYSCALL_VFSQUERY			0x3B
#define MOLLENOS_SYSCALL_VFSPATH			0x3C

#define MOLLENOS_SYSCALL_DEVQUERY			0x51

#define MOLLENOS_SYSCALL_ENDBOOT			0x5B
#define MOLLENOS_SYSCALL_REGWM				0x5C

/* Prototypes */
_CRT_EXTERN int Syscall0(int Function);
_CRT_EXTERN int Syscall1(int Function, int Arg0);
_CRT_EXTERN int Syscall2(int Function, int Arg0, int Arg1);
_CRT_EXTERN int Syscall3(int Function, int Arg0, int Arg1, int Arg2);
_CRT_EXTERN int Syscall4(int Function, int Arg0, int Arg1, int Arg2, int Arg3);
_CRT_EXTERN int Syscall5(int Function, int Arg0, int Arg1, int Arg2, int Arg3, int Arg4);

#endif //!_MOLLENOS_SYSCALLS_H_
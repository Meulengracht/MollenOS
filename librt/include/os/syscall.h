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
#define SYSCALL_PARAM(Arg)			((int)Arg)

/* Process System Calls */
#define SYSCALL_TERMINATE			0x1
#define SYSCALL_PROCQUERY			0x2
#define SYSCALL_PROCSPAWN			0x3
#define SYSCALL_PROCJOIN			0x4
#define SYSCALL_PROCKILL			0x5
#define SYSCALL_PROCSIGNAL			0x6
#define SYSCALL_PROCRAISE			0x7

/* Shared Object System Calls */
#define SYSCALL_LOADSO				0x8
#define SYSCALL_LOADPROC			0x9
#define SYSCALL_UNLOADSO			0xA

/* Threading System Calls */
#define SYSCALL_THREADCREATE		0xB
#define SYSCALL_THREADEXIT			0xC
#define SYSCALL_THREADKILL			0xD
#define SYSCALL_THREADJOIN			0xE
#define SYSCALL_THREADSLEEP			0xF
#define SYSCALL_THREADYIELD			0x10
#define SYSCALL_THREADID			0x11

/* Synchronization System Calls */
#define SYSCALL_CONDCREATE			0x15
#define SYSCALL_CONDDESTROY			0x16
#define SYSCALL_SYNCSLEEP			0x17
#define SYSCALL_SYNCWAKEONE			0x18
#define SYSCALL_SYNCWAKEALL			0x19

/* Memory System Calls */
#define SYSCALL_MEMALLOC			0x1F
#define SYSCALL_MEMFREE				0x20
#define SYSCALL_MEMQUERY			0x21
#define SYSCALL_MEMSHARE			0x22
#define SYSCALL_MEMUNSHARE			0x23

/* IPC System Calls */
#define SYSCALL_OPENPIPE			0x29
#define SYSCALL_CLOSEPIPE			0x2A
#define SYSCALL_READPIPE			0x2B
#define SYSCALL_WRITEPIPE			0x2C
#define SYSCALL_PSIGWAIT			0x2D
#define SYSCALL_PSIGSEND			0x2E
#define SYSCALL_RPCEVAL				0x2F
#define SYSCALL_RPCWAIT				0x30
#define SYSCALL_EVTEXEC				0x31

/* VFS System Calls */
#define SYSCALL_VFSOPEN				0x33
#define SYSCALL_VFSCLOSE			0x34
#define SYSCALL_VFSREAD				0x35
#define SYSCALL_VFSWRITE			0x36
#define SYSCALL_VFSSEEK				0x37
#define SYSCALL_VFSFLUSH			0x38
#define SYSCALL_VFSDELETE			0x39
#define SYSCALL_VFSMOVE				0x3A
#define SYSCALL_VFSQUERY			0x3B
#define SYSCALL_VFSPATH				0x3C

#define SYSCALL_DEVQUERY			0x51

#define SYSCALL_ENDBOOT				0x5B
#define SYSCALL_REGWM				0x5C
#define SYSCALL_SYSTEMQUERY			0x5D

/* Driver System Calls 
 * - ACPI Support */
#define SYSCALL_ACPIQUERY			0x65
#define SYSCALL_ACPIGETTBLHEADER	0x66
#define SYSCALL_ACPIGETTBLFULL		0x67

/* Driver System Calls 
 * - I/O Support */
#define SYSCALL_IOSREGISTER			0x6F
#define SYSCALL_IOSACQUIRE			0x70
#define SYSCALL_IOSRELEASE			0x71
#define SYSCALL_IOSDESTROY			0x72

/* Driver System Calls 
 * - Server Support */
#define SYSCALL_SERVERREGISTER		0x73

/* Prototypes */
_MOS_API int Syscall0(int Function);
_MOS_API int Syscall1(int Function, int Arg0);
_MOS_API int Syscall2(int Function, int Arg0, int Arg1);
_MOS_API int Syscall3(int Function, int Arg0, int Arg1, int Arg2);
_MOS_API int Syscall4(int Function, int Arg0, int Arg1, int Arg2, int Arg3);
_MOS_API int Syscall5(int Function, int Arg0, int Arg1, int Arg2, int Arg3, int Arg4);

#endif //!_MOLLENOS_SYSCALLS_H_
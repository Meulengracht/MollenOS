/* MollenOS
 *
 * Copyright 2011 - 2017, Philip Meulengracht
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
 * MollenOS MCore - Syscalls Support Definitions & Structures
 * - This header describes the base syscall-structures, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#ifndef _SYSCALL_INTEFACE_H_
#define _SYSCALL_INTEFACE_H_

/* Includes 
 * - Library */
#include <os/osdefs.h>

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
#define SYSCALL_THREADSIGNAL		0xD
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
#define SYSCALL_MEMACQUIRE			0x22
#define SYSCALL_MEMRELEASE			0x23

/* Path System Calls */
#define SYSCALL_QUERYCWD			0x26
#define SYSCALL_CHANGECWD			0x27
#define SYSCALL_QUERYCAD			0x28

/* IPC System Calls */
#define SYSCALL_OPENPIPE			0x29
#define SYSCALL_CLOSEPIPE			0x2A
#define SYSCALL_READPIPE			0x2B
#define SYSCALL_WRITEPIPE			0x2C
#define SYSCALL_PSIGWAIT			0x2D
#define SYSCALL_PSIGSEND			0x2E
#define SYSCALL_RPCEVAL				0x2F
#define SYSCALL_RPCWAIT				0x30

#define SYSCALL_ENDBOOT				0x33
#define SYSCALL_SYSTEMQUERY			0x35

/* Driver System Calls 
 * - ACPI Support */
#define SYSCALL_ACPIQUERY			0x3D
#define SYSCALL_ACPIGETTBLHEADER	0x3E
#define SYSCALL_ACPIGETTBLFULL		0x3F
#define SYSCALL_ACPIQUERYIRQ		0x40

/* Driver System Calls 
 * - I/O Support */
#define SYSCALL_IOSREGISTER			0x47
#define SYSCALL_IOSACQUIRE			0x48
#define SYSCALL_IOSRELEASE			0x49
#define SYSCALL_IOSDESTROY			0x4A

/* Driver System Calls 
 * - Server Support */
#define SYSCALL_SERVICEREGISTER		0x4B
#define SYSCALL_RESOLVEDRIVER		0x4C

/* Driver System Calls 
 * - Interrupt Support */
#define SYSCALL_REGISTERIRQ			0x51
#define SYSCALL_UNREGISTERIRQ		0x52
#define SYSCALL_ACKNOWLEDGEIRQ		0x53
#define SYSCALL_REGISTERSYSTMR		0x54

 /* Start one of these before function prototypes */
_CODE_BEGIN

/* Prototypes */
MOSAPI int Syscall0(int Function);
MOSAPI int Syscall1(int Function, int Arg0);
MOSAPI int Syscall2(int Function, int Arg0, int Arg1);
MOSAPI int Syscall3(int Function, int Arg0, int Arg1, int Arg2);
MOSAPI int Syscall4(int Function, int Arg0, int Arg1, int Arg2, int Arg3);
MOSAPI int Syscall5(int Function, int Arg0, int Arg1, int Arg2, int Arg3, int Arg4);

_CODE_END

#endif //!_SYSCALL_INTEFACE_H_

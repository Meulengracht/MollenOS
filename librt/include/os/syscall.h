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

/* System Call Definitions 
 * Helpers, macros and definitions for system calls */
#define SCPARAM(Arg)                ((int)Arg)
_CODE_BEGIN
MOSAPI int syscall0(int Function);
MOSAPI int syscall1(int Function, int Arg0);
MOSAPI int syscall2(int Function, int Arg0, int Arg1);
MOSAPI int syscall3(int Function, int Arg0, int Arg1, int Arg2);
MOSAPI int syscall4(int Function, int Arg0, int Arg1, int Arg2, int Arg3);
MOSAPI int syscall5(int Function, int Arg0, int Arg1, int Arg2, int Arg3, int Arg4);
_CODE_END

/* Process system calls
 * - Process related system call definitions */
#define Syscall_ProcessExit(ExitCode) (OsStatus_t)syscall1(1, SCPARAM(ExitCode))
#define Syscall_ProcessQuery(ProcessId, Function, Buffer, Length) (OsStatus_t)syscall4(2, SCPARAM(ProcessId), SCPARAM(Function), SCPARAM(Buffer), SCPARAM(Length))
#define Syscall_ProcessSpawn(Path, StartupInformation, Async) (UUId_t)syscall3(3, SCPARAM(Path), SCPARAM(StartupInformation), SCPARAM(Async))
#define Syscall_ProcessJoin(ProcessId) syscall1(4, SCPARAM(ProcessId))
#define Syscall_ProcessKill(ProcessId) (OsStatus_t)syscall1(5, SCPARAM(ProcessId))
#define Syscall_ProcessSignal(Signal, Handler) (uintptr_t)syscall2(6, SCPARAM(Signal), SCPARAM(Handler))
#define Syscall_ProcessRaise(ProcessId, Signal) (OsStatus_t)syscall2(7, SCPARAM(ProcessId), SCPARAM(Signal))

/* SharedObject system calls
 * - SharedObject related system call definitions */
#define Syscall_LibraryLoad(Path) (Handle_t)syscall1(8, SCPARAM(Path))
#define Syscall_LibraryFunction(Handle, FunctionName) (uintptr_t)syscall2(9, SCPARAM(Handle), SCPARAM(FunctionName))
#define Syscall_LibraryUnload(Handle) (OsStatus_t)syscall1(10, SCPARAM(Handle))

/* Threading system calls
 * - Threading related system call definitions */
#define Syscall_ThreadCreate(Entry, Argument, Flags) (UUId_t)syscall3(11, SCPARAM(Entry), SCPARAM(Argument), SCPARAM(Flags))
#define Syscall_ThreadExit(ExitCode) (OsStatus_t)syscall1(12, SCPARAM(ExitCode))
#define Syscall_ThreadSignal(ThreadId, Signal) (OsStatus_t)syscall2(13, SCPARAM(ThreadId), SCPARAM(Signal))
#define Syscall_ThreadJoin(ThreadId, ExitCode) (OsStatus_t)syscall2(14, SCPARAM(ThreadId), SCPARAM(ExitCode))
#define Syscall_ThreadSleep(Milliseconds) (OsStatus_t)syscall1(15, SCPARAM(Milliseconds))
#define Syscall_ThreadYield() (OsStatus_t)syscall0(16)
#define Syscall_ThreadId() (UUId_t)syscall0(17)
#define Syscall_GetStartInfo(StartupInformation) (OsStatus_t)syscall1(18, SCPARAM(StartupInformation))

/* Condition system calls
 * - Condition related system call definitions */
#define Syscall_ConditionCreate() (Handle_t)syscall0(21)
#define Syscall_ConditionDestroy(Handle) (OsStatus_t)syscall1(22, SCPARAM(Handle))
#define Syscall_WaitForObject(Handle, Timeout) (OsStatus_t)syscall2(23, SCPARAM(Handle), SCPARAM(Timeout))
#define Syscall_SignalHandle(Handle) (OsStatus_t)syscall1(24, SCPARAM(Handle))
#define Syscall_BroadcastHandle(Handle) (OsStatus_t)syscall1(25, SCPARAM(Handle))

/* Memory System Calls */
#define SYSCALL_MEMALLOC            0x1F
#define SYSCALL_MEMFREE                0x20
#define SYSCALL_MEMQUERY            0x21
#define SYSCALL_MEMACQUIRE            0x22
#define SYSCALL_MEMRELEASE            0x23

/* Path System Calls */
#define SYSCALL_QUERYCWD            0x26
#define SYSCALL_CHANGECWD            0x27
#define SYSCALL_QUERYCAD            0x28

/* IPC System Calls */
#define SYSCALL_OPENPIPE            0x29
#define SYSCALL_CLOSEPIPE            0x2A
#define SYSCALL_READPIPE            0x2B
#define SYSCALL_WRITEPIPE            0x2C
#define SYSCALL_PSIGWAIT            0x2D
#define SYSCALL_PSIGSEND            0x2E
#define SYSCALL_RPCEVAL                0x2F
#define SYSCALL_RPCWAIT                0x30
#define SYSCALL_REMOTECALLLISTEN    0x31

#define SYSCALL_ENDBOOT                0x33
#define SYSCALL_SYSTEMQUERY            0x35
#define SYSCALL_GETTICK             0x36
#define SYSCALL_GETPERFFREQ         0x37
#define SYSCALL_GETPERFTICK         0x38
#define SYSCALL_GETTIME             0x39

/* Driver System Calls 
 * - ACPI Support */
#define SYSCALL_ACPIQUERY            0x3D
#define SYSCALL_ACPIGETTBLHEADER    0x3E
#define SYSCALL_ACPIGETTBLFULL        0x3F
#define SYSCALL_ACPIQUERYIRQ        0x40

/* Driver System Calls 
 * - I/O Support */
#define SYSCALL_IOSREGISTER            0x47
#define SYSCALL_IOSACQUIRE            0x48
#define SYSCALL_IOSRELEASE            0x49
#define SYSCALL_IOSDESTROY            0x4A

/* Driver System Calls 
 * - Server Support */
#define SYSCALL_SERVICEREGISTER        0x4B
#define SYSCALL_RESOLVEDRIVER        0x4C

/* Driver System Calls 
 * - Interrupt Support */
#define SYSCALL_REGISTERIRQ            0x51
#define SYSCALL_UNREGISTERIRQ        0x52
#define SYSCALL_TIMERSTART          0x55
#define SYSCALL_TIMERSTOP           0x56

#endif //!_SYSCALL_INTEFACE_H_

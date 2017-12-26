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
CRTDECL(int, syscall0(int Function));
CRTDECL(int, syscall1(int Function, int Arg0));
CRTDECL(int, syscall2(int Function, int Arg0, int Arg1));
CRTDECL(int, syscall3(int Function, int Arg0, int Arg1, int Arg2));
CRTDECL(int, syscall4(int Function, int Arg0, int Arg1, int Arg2, int Arg3));
CRTDECL(int, syscall5(int Function, int Arg0, int Arg1, int Arg2, int Arg3, int Arg4));
_CODE_END

/* Process system calls
 * - Process related system call definitions */
#define Syscall_ProcessExit(ExitCode) (OsStatus_t)syscall1(1, SCPARAM(ExitCode))
#define Syscall_ProcessQuery(ProcessId, Function, Buffer, Length) (OsStatus_t)syscall4(2, SCPARAM(ProcessId), SCPARAM(Function), SCPARAM(Buffer), SCPARAM(Length))
#define Syscall_ProcessSpawn(Path, StartupInformation, Async) (UUId_t)syscall3(3, SCPARAM(Path), SCPARAM(StartupInformation), SCPARAM(Async))
#define Syscall_ProcessJoin(ProcessId) syscall1(4, SCPARAM(ProcessId))
#define Syscall_ProcessKill(ProcessId) (OsStatus_t)syscall1(5, SCPARAM(ProcessId))
#define Syscall_ProcessSignal(Handler) (OsStatus_t)syscall1(6, SCPARAM(Handler))
#define Syscall_ProcessRaise(ProcessId, Signal) (OsStatus_t)syscall2(7, SCPARAM(ProcessId), SCPARAM(Signal))
#define Syscall_ProcessGetModuleHandles(HandleList) (OsStatus_t)syscall1(8, SCPARAM(HandleList))
#define Syscall_ProcessGetModuleEntryPoints(HandleList) (OsStatus_t)syscall1(9, SCPARAM(HandleList))
#define Syscall_ProcessGetStartupInfo(StartupInformation) (OsStatus_t)syscall1(10, SCPARAM(StartupInformation))

/* SharedObject system calls
 * - SharedObject related system call definitions */
#define Syscall_LibraryLoad(Path) (Handle_t)syscall1(11, SCPARAM(Path))
#define Syscall_LibraryFunction(Handle, FunctionName) (uintptr_t)syscall2(12, SCPARAM(Handle), SCPARAM(FunctionName))
#define Syscall_LibraryUnload(Handle) (OsStatus_t)syscall1(13, SCPARAM(Handle))

/* Threading system calls
 * - Threading related system call definitions */
#define Syscall_ThreadCreate(Entry, Argument, Flags) (UUId_t)syscall3(14, SCPARAM(Entry), SCPARAM(Argument), SCPARAM(Flags))
#define Syscall_ThreadExit(ExitCode) (OsStatus_t)syscall1(15, SCPARAM(ExitCode))
#define Syscall_ThreadSignal(ThreadId, Signal) (OsStatus_t)syscall2(16, SCPARAM(ThreadId), SCPARAM(Signal))
#define Syscall_ThreadJoin(ThreadId, ExitCode) (OsStatus_t)syscall2(17, SCPARAM(ThreadId), SCPARAM(ExitCode))
#define Syscall_ThreadSleep(Milliseconds, MillisecondsSlept) (OsStatus_t)syscall2(18, SCPARAM(Milliseconds), SCPARAM(MillisecondsSlept))
#define Syscall_ThreadYield() (OsStatus_t)syscall0(19)
#define Syscall_ThreadId() (UUId_t)syscall0(20)

/* Condition system calls
 * - Condition related system call definitions */
#define Syscall_ConditionCreate(Handle) (OsStatus_t)syscall1(21, SCPARAM(Handle))
#define Syscall_ConditionDestroy(Handle) (OsStatus_t)syscall1(22, SCPARAM(Handle))
#define Syscall_WaitForObject(Handle, Timeout) (OsStatus_t)syscall2(23, SCPARAM(Handle), SCPARAM(Timeout))
#define Syscall_SignalHandle(Handle) (OsStatus_t)syscall1(24, SCPARAM(Handle))
#define Syscall_BroadcastHandle(Handle) (OsStatus_t)syscall1(25, SCPARAM(Handle))

/* Memory system calls
 * - Memory related system call definitions */
#define Syscall_MemoryAllocate(Size, Flags, Virtual, Physical) (OsStatus_t)syscall4(31, SCPARAM(Size), SCPARAM(Flags), SCPARAM(Virtual), SCPARAM(Physical))
#define Syscall_MemoryFree(Pointer, Size) (OsStatus_t)syscall2(32, SCPARAM(Pointer), SCPARAM(Size))
#define Syscall_MemoryQuery(MemoryInformation) (OsStatus_t)syscall1(33, SCPARAM(MemoryInformation))
#define Syscall_MemoryAcquire(Physical, Size, Virtual) (OsStatus_t)syscall3(34, SCPARAM(Physical), SCPARAM(Size), SCPARAM(Virtual))
#define Syscall_MemoryRelease(Virtual, Size) (OsStatus_t)syscall2(35, SCPARAM(Virtual), SCPARAM(Size))

/* Path system calls
 * - Path related system call definitions */
#define Syscall_QueryWorkingPath(Buffer, MaxLength) (OsStatus_t)syscall2(38, SCPARAM(Buffer), SCPARAM(MaxLength))
#define Syscall_ChangeWorkingPath(Path) (OsStatus_t)syscall1(39, SCPARAM(Path))
#define Syscall_QueryApplicationPath(Buffer, MaxLength) (OsStatus_t)syscall2(40, SCPARAM(Buffer), SCPARAM(MaxLength))

/* Communication system calls
 * - Communication related system call definitions */
#define Syscall_PipeOpen(Port, Flags) (OsStatus_t)syscall2(41, SCPARAM(Port), SCPARAM(Flags))
#define Syscall_PipeClose(Port) (OsStatus_t)syscall1(42, SCPARAM(Port))
#define Syscall_PipeRead(Port, Buffer, Length) (OsStatus_t)syscall3(43, SCPARAM(Port), SCPARAM(Buffer), SCPARAM(Length))
#define Syscall_PipeWrite(ProcessId, Port, Buffer, Length) (OsStatus_t)syscall4(44, SCPARAM(ProcessId), SCPARAM(Port), SCPARAM(Buffer), SCPARAM(Length))
#define Syscall_IpcWait(Timeout) (OsStatus_t)syscall1(45, SCPARAM(Timeout))
#define Syscall_IpcWake(ProcessId) (OsStatus_t)syscall1(46, SCPARAM(ProcessId))
#define Syscall_RemoteCall(RemoteCall, Asynchronous) (OsStatus_t)syscall2(47, SCPARAM(RemoteCall), SCPARAM(Asynchronous))
#define Syscall_RpcGetResponse(RemoteCall) (OsStatus_t)syscall1(48, SCPARAM(RemoteCall))
#define Syscall_RemoteCallWait(Port, RemoteCall, ArgumentBuffer) (OsStatus_t)syscall3(49, SCPARAM(Port), SCPARAM(RemoteCall), SCPARAM(ArgumentBuffer))
#define Syscall_RemoteCallRespond(RemoteCall, Buffer, Length) (OsStatus_t)syscall3(50, SCPARAM(RemoteCall), SCPARAM(Buffer), SCPARAM(Length))

/* Base system calls
 * - Base related system call definitions */
#define Syscall_Debug(Type, Module, Message) (OsStatus_t)syscall3(0, SCPARAM(Type), SCPARAM(Module), SCPARAM(Message))
#define Syscall_SystemStart() (OsStatus_t)syscall0(51)
#define Syscall_SystemQuery() (OsStatus_t)syscall0(53)
#define Syscall_SystemTick(Tick) (OsStatus_t)syscall1(54, SCPARAM(Tick))
#define Syscall_SystemPerformanceFrequency(Frequency) (OsStatus_t)syscall1(55, SCPARAM(Frequency))
#define Syscall_SystemPerformanceTime(Value) (OsStatus_t)syscall1(56, SCPARAM(Value))
#define Syscall_SystemTime(Time) (OsStatus_t)syscall1(57, SCPARAM(Time))

/* Driver system calls
 * - ACPI related system call definitions */
#define Syscall_AcpiQuery(Descriptor) (OsStatus_t)syscall1(61, SCPARAM(Descriptor))
#define Syscall_AcpiGetHeader(Signature, Header) (OsStatus_t)syscall2(62, SCPARAM(Signature), SCPARAM(Header))
#define Syscall_AcpiGetTable(Signature, Table) (OsStatus_t)syscall2(63, SCPARAM(Signature), SCPARAM(Table))
#define Syscall_AcpiQueryInterrupt(Bus, Slot, Pin, Interrupt, Conform) (OsStatus_t)syscall5(64, SCPARAM(Bus), SCPARAM(Slot), SCPARAM(Pin), SCPARAM(Interrupt), SCPARAM(Conform))

/* Driver system calls
 * - I/O related system call definitions */
#define Syscall_IoSpaceRegister(IoSpace) (OsStatus_t)syscall1(71, SCPARAM(IoSpace))
#define Syscall_IoSpaceAcquire(IoSpace) (OsStatus_t)syscall1(72, SCPARAM(IoSpace))
#define Syscall_IoSpaceRelease(IoSpace) (OsStatus_t)syscall1(73, SCPARAM(IoSpace))
#define Syscall_IoSpaceDestroy(IoSpaceId) (OsStatus_t)syscall1(74, SCPARAM(IoSpaceId))

/* Driver system calls
 * - Server related system call definitions */
#define Syscall_RegisterService(Alias) (OsStatus_t)syscall1(75, SCPARAM(Alias))
#define Syscall_LoadDriver(Device, Length) (OsStatus_t)syscall2(76, SCPARAM(Device), SCPARAM(Length))

/* Driver system calls
 * - Interrupt related system call definitions */
#define Syscall_InterruptAdd(Descriptor, Flags) (UUId_t)syscall2(81, SCPARAM(Descriptor), SCPARAM(Flags))
#define Syscall_InterruptRemove(InterruptId) (OsStatus_t)syscall1(82, SCPARAM(InterruptId))
#define Syscall_TimerCreate(Interval, Periodic, Context) (UUId_t)syscall3(85, SCPARAM(Interval), SCPARAM(Periodic), SCPARAM(Context))
#define Syscall_TimerStop(TimerId) (OsStatus_t)syscall1(86, SCPARAM(TimerId))

#endif //!_SYSCALL_INTEFACE_H_

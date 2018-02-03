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
#define Syscall_ProcessId(ProcessId) (OsStatus_t)syscall1(2, SCPARAM(ProcessId))
#define Syscall_ProcessSpawn(Path, StartupInformation, Async) (UUId_t)syscall3(3, SCPARAM(Path), SCPARAM(StartupInformation), SCPARAM(Async))
#define Syscall_ProcessJoin(ProcessId, Timeout, ExitCode) (OsStatus_t)syscall3(4, SCPARAM(ProcessId), SCPARAM(Timeout), SCPARAM(ExitCode))
#define Syscall_ProcessKill(ProcessId) (OsStatus_t)syscall1(5, SCPARAM(ProcessId))
#define Syscall_ProcessSignal(Handler) (OsStatus_t)syscall1(6, SCPARAM(Handler))
#define Syscall_ProcessRaise(ProcessId, Signal) (OsStatus_t)syscall2(7, SCPARAM(ProcessId), SCPARAM(Signal))
#define Syscall_ProcessName(Buffer, MaxLength) (OsStatus_t)syscall2(8, SCPARAM(Buffer), SCPARAM(MaxLength))
#define Syscall_ProcessGetModuleHandles(HandleList) (OsStatus_t)syscall1(9, SCPARAM(HandleList))
#define Syscall_ProcessGetModuleEntryPoints(HandleList) (OsStatus_t)syscall1(10, SCPARAM(HandleList))
#define Syscall_ProcessGetStartupInfo(StartupInformation) (OsStatus_t)syscall1(11, SCPARAM(StartupInformation))

/* SharedObject system calls
 * - SharedObject related system call definitions */
#define Syscall_LibraryLoad(Path) (Handle_t)syscall1(12, SCPARAM(Path))
#define Syscall_LibraryFunction(Handle, FunctionName) (uintptr_t)syscall2(13, SCPARAM(Handle), SCPARAM(FunctionName))
#define Syscall_LibraryUnload(Handle) (OsStatus_t)syscall1(14, SCPARAM(Handle))

/* Threading system calls
 * - Threading related system call definitions */
#define Syscall_ThreadCreate(Entry, Argument, Flags) (UUId_t)syscall3(15, SCPARAM(Entry), SCPARAM(Argument), SCPARAM(Flags))
#define Syscall_ThreadExit(ExitCode) (OsStatus_t)syscall1(16, SCPARAM(ExitCode))
#define Syscall_ThreadSignal(ThreadId, Signal) (OsStatus_t)syscall2(17, SCPARAM(ThreadId), SCPARAM(Signal))
#define Syscall_ThreadJoin(ThreadId, ExitCode) (OsStatus_t)syscall2(18, SCPARAM(ThreadId), SCPARAM(ExitCode))
#define Syscall_ThreadDetach(ThreadId) (OsStatus_t)syscall1(19, SCPARAM(ThreadId))
#define Syscall_ThreadSleep(Milliseconds, MillisecondsSlept) (OsStatus_t)syscall2(20, SCPARAM(Milliseconds), SCPARAM(MillisecondsSlept))
#define Syscall_ThreadYield() (OsStatus_t)syscall0(21)
#define Syscall_ThreadId() (UUId_t)syscall0(22)
#define Syscall_ThreadSetCurrentName(Name) (UUId_t)syscall1(23, SCPARAM(Name))
#define Syscall_ThreadGetCurrentName(NameBuffer, MaxLength) (UUId_t)syscall2(24, SCPARAM(NameBuffer), SCPARAM(MaxLength))

/* Condition system calls
 * - Condition related system call definitions */
#define Syscall_ConditionCreate(Handle) (OsStatus_t)syscall1(31, SCPARAM(Handle))
#define Syscall_ConditionDestroy(Handle) (OsStatus_t)syscall1(32, SCPARAM(Handle))
#define Syscall_WaitForObject(Handle, Timeout) (OsStatus_t)syscall2(33, SCPARAM(Handle), SCPARAM(Timeout))
#define Syscall_SignalHandle(Handle) (OsStatus_t)syscall1(34, SCPARAM(Handle))
#define Syscall_BroadcastHandle(Handle) (OsStatus_t)syscall1(35, SCPARAM(Handle))

/* Memory system calls
 * - Memory related system call definitions */
#define Syscall_MemoryAllocate(Size, Flags, Virtual, Physical) (OsStatus_t)syscall4(41, SCPARAM(Size), SCPARAM(Flags), SCPARAM(Virtual), SCPARAM(Physical))
#define Syscall_MemoryFree(Pointer, Size) (OsStatus_t)syscall2(42, SCPARAM(Pointer), SCPARAM(Size))
#define Syscall_MemoryQuery(MemoryInformation) (OsStatus_t)syscall1(43, SCPARAM(MemoryInformation))
#define Syscall_MemoryAcquire(Physical, Size, Virtual) (OsStatus_t)syscall3(44, SCPARAM(Physical), SCPARAM(Size), SCPARAM(Virtual))
#define Syscall_MemoryRelease(Virtual, Size) (OsStatus_t)syscall2(45, SCPARAM(Virtual), SCPARAM(Size))
#define Syscall_MemoryProtect(MemoryPointer, Length, Flags, PreviousFlags) (OsStatus_t)syscall4(46, SCPARAM(MemoryPointer), SCPARAM(Length), SCPARAM(Flags), SCPARAM(PreviousFlags))

/* Operating support system calls
 * - Operating support related system call definitions */
#define Syscall_GetWorkingDirectory(Buffer, MaxLength) (OsStatus_t)syscall2(51, SCPARAM(Buffer), SCPARAM(MaxLength))
#define Syscall_SetWorkingDirectory(Path) (OsStatus_t)syscall1(52, SCPARAM(Path))
#define Syscall_GetAssemblyDirectory(Buffer, MaxLength) (OsStatus_t)syscall2(53, SCPARAM(Buffer), SCPARAM(MaxLength))
#define Syscall_CreateFileMapping(MappingParameters, MemoryPointer) (OsStatus_t)syscall2(54, SCPARAM(MappingParameters), SCPARAM(MemoryPointer))
#define Syscall_DestroyFileMapping(MemoryPointer) (OsStatus_t)syscall1(55, SCPARAM(MemoryPointer))

/* Communication system calls
 * - Communication related system call definitions */
#define Syscall_PipeOpen(Port, Flags) (OsStatus_t)syscall2(61, SCPARAM(Port), SCPARAM(Flags))
#define Syscall_PipeClose(Port) (OsStatus_t)syscall1(62, SCPARAM(Port))
#define Syscall_PipeRead(Port, Buffer, Length) (OsStatus_t)syscall3(63, SCPARAM(Port), SCPARAM(Buffer), SCPARAM(Length))
#define Syscall_PipeSend(ProcessId, Port, Buffer, Length) (OsStatus_t)syscall4(64, SCPARAM(ProcessId), SCPARAM(Port), SCPARAM(Buffer), SCPARAM(Length))
#define Syscall_PipeReceive(ProcessId, Port, Buffer, Length) (OsStatus_t)syscall4(65, SCPARAM(ProcessId), SCPARAM(Port), SCPARAM(Buffer), SCPARAM(Length))
#define Syscall_RemoteCall(RemoteCall, Asynchronous) (OsStatus_t)syscall2(67, SCPARAM(RemoteCall), SCPARAM(Asynchronous))
#define Syscall_RpcGetResponse(RemoteCall) (OsStatus_t)syscall1(68, SCPARAM(RemoteCall))
#define Syscall_RemoteCallWait(Port, RemoteCall, ArgumentBuffer) (OsStatus_t)syscall3(69, SCPARAM(Port), SCPARAM(RemoteCall), SCPARAM(ArgumentBuffer))
#define Syscall_RemoteCallRespond(RemoteCall, Buffer, Length) (OsStatus_t)syscall3(70, SCPARAM(RemoteCall), SCPARAM(Buffer), SCPARAM(Length))

/* Base system calls
 * - Base related system call definitions */
#define Syscall_Debug(Type, Module, Message) (OsStatus_t)syscall3(0, SCPARAM(Type), SCPARAM(Module), SCPARAM(Message))
#define Syscall_SystemStart() (OsStatus_t)syscall0(71)
#define Syscall_FlushHardwareCache(CacheType, AddressStart, Length) (OsStatus_t)syscall3(72, SCPARAM(CacheType), SCPARAM(AddressStart), SCPARAM(Length))
#define Syscall_SystemQuery() (OsStatus_t)syscall0(73)
#define Syscall_SystemTick(Tick) (OsStatus_t)syscall1(74, SCPARAM(Tick))
#define Syscall_SystemPerformanceFrequency(Frequency) (OsStatus_t)syscall1(75, SCPARAM(Frequency))
#define Syscall_SystemPerformanceTime(Value) (OsStatus_t)syscall1(76, SCPARAM(Value))
#define Syscall_SystemTime(Time) (OsStatus_t)syscall1(77, SCPARAM(Time))

/* Driver system calls
 * - ACPI related system call definitions */
#define Syscall_AcpiQuery(Descriptor) (OsStatus_t)syscall1(81, SCPARAM(Descriptor))
#define Syscall_AcpiGetHeader(Signature, Header) (OsStatus_t)syscall2(82, SCPARAM(Signature), SCPARAM(Header))
#define Syscall_AcpiGetTable(Signature, Table) (OsStatus_t)syscall2(83, SCPARAM(Signature), SCPARAM(Table))
#define Syscall_AcpiQueryInterrupt(Bus, Slot, Pin, Interrupt, Conform) (OsStatus_t)syscall5(84, SCPARAM(Bus), SCPARAM(Slot), SCPARAM(Pin), SCPARAM(Interrupt), SCPARAM(Conform))

/* Driver system calls
 * - I/O related system call definitions */
#define Syscall_IoSpaceRegister(IoSpace) (OsStatus_t)syscall1(91, SCPARAM(IoSpace))
#define Syscall_IoSpaceAcquire(IoSpace) (OsStatus_t)syscall1(92, SCPARAM(IoSpace))
#define Syscall_IoSpaceRelease(IoSpace) (OsStatus_t)syscall1(93, SCPARAM(IoSpace))
#define Syscall_IoSpaceDestroy(IoSpaceId) (OsStatus_t)syscall1(94, SCPARAM(IoSpaceId))

/* Driver system calls
 * - Server related system call definitions */
#define Syscall_RegisterService(Alias) (OsStatus_t)syscall1(95, SCPARAM(Alias))
#define Syscall_LoadDriver(Device, Length) (OsStatus_t)syscall2(96, SCPARAM(Device), SCPARAM(Length))

/* Driver system calls
 * - Interrupt related system call definitions */
#define Syscall_InterruptAdd(Descriptor, Flags) (UUId_t)syscall2(101, SCPARAM(Descriptor), SCPARAM(Flags))
#define Syscall_InterruptRemove(InterruptId) (OsStatus_t)syscall1(102, SCPARAM(InterruptId))
#define Syscall_TimerCreate(Interval, Periodic, Context) (UUId_t)syscall3(105, SCPARAM(Interval), SCPARAM(Periodic), SCPARAM(Context))
#define Syscall_TimerStop(TimerId) (OsStatus_t)syscall1(106, SCPARAM(TimerId))

#endif //!_SYSCALL_INTEFACE_H_

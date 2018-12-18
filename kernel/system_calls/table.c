/* MollenOS
 *
 * Copyright 2011, Philip Meulengracht
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
 * MollenOS MCore - System Calls
 */
#define DefineSyscall(Index, Fn) ((uintptr_t)&Fn)

#include <os/contracts/video.h>
#include <os/mollenos.h>
#include <os/process.h>
#include <os/buffer.h>
#include <threading.h>
#include <os/input.h>
#include <os/acpi.h>
#include <time.h>

struct MemoryMappingParameters;

///////////////////////////////////////////////
// Operating System Interface
// - Protected, services/modules

// System system calls
OsStatus_t ScSystemDebug(int Type, const char* Module, const char* Message);
OsStatus_t ScEndBootSequence(void);
OsStatus_t ScQueryDisplayInformation(VideoDescriptor_t *Descriptor);
void*      ScCreateDisplayFramebuffer(void);

// Module system calls
OsStatus_t ScModuleGetStartupInformation(ProcessStartupInformation_t* StartupInformation);
OsStatus_t ScModuleGetCurrentId(UUId_t* Handle);
OsStatus_t ScModuleGetCurrentName(const char* Buffer, size_t MaxLength);
OsStatus_t ScModuleGetModuleHandles(Handle_t ModuleList[PROCESS_MAXMODULES]);
OsStatus_t ScModuleGetModuleEntryPoints(Handle_t ModuleList[PROCESS_MAXMODULES]);
OsStatus_t ScModuleExit(int ExitCode);

OsStatus_t ScSharedObjectLoad(const char* SoName, uint8_t* Buffer, size_t BufferLength, Handle_t* HandleOut);
uintptr_t  ScSharedObjectGetFunction(Handle_t Handle, const char* Function);
OsStatus_t ScSharedObjectUnload(Handle_t Handle);

OsStatus_t ScGetWorkingDirectory(char* PathBuffer, size_t MaxLength);
OsStatus_t ScSetWorkingDirectory(const char* Path);
OsStatus_t ScGetAssemblyDirectory(char* PathBuffer, size_t MaxLength);

OsStatus_t ScCreateMemorySpace(Flags_t Flags, UUId_t* Handle);
OsStatus_t ScGetThreadMemorySpaceHandle(UUId_t ThreadHandle, UUId_t* Handle);
OsStatus_t ScCreateMemorySpaceMapping(UUId_t Handle, struct MemoryMappingParameters* Parameters, DmaBuffer_t* AccessBuffer);

// Driver system calls
OsStatus_t ScAcpiQueryStatus(AcpiDescriptor_t* AcpiDescriptor);
OsStatus_t ScAcpiQueryTableHeader(const char* Signature, ACPI_TABLE_HEADER* Header);
OsStatus_t ScAcpiQueryTable(const char* Signature, ACPI_TABLE_HEADER* Table);
OsStatus_t ScAcpiQueryInterrupt(DevInfo_t Bus, DevInfo_t Device, int Pin, int* Interrupt, Flags_t* AcpiConform);
OsStatus_t ScIoSpaceRegister(DeviceIo_t* IoSpace);
OsStatus_t ScIoSpaceAcquire(DeviceIo_t* IoSpace);
OsStatus_t ScIoSpaceRelease(DeviceIo_t* IoSpace);
OsStatus_t ScIoSpaceDestroy(DeviceIo_t* IoSpace);
OsStatus_t ScRegisterAliasId(UUId_t Alias);
OsStatus_t ScLoadDriver(MCoreDevice_t* Device, size_t Length);
UUId_t     ScRegisterInterrupt(DeviceInterrupt_t* Interrupt, Flags_t Flags);
OsStatus_t ScUnregisterInterrupt(UUId_t Source);
OsStatus_t ScRegisterEventTarget(UUId_t KeyInput, UUId_t WmInput);
OsStatus_t ScKeyEvent(SystemKey_t* Key);
OsStatus_t ScInputEvent(SystemInput_t* Input);
UUId_t     ScTimersStart(size_t Interval, int Periodic, const void* Data);
OsStatus_t ScTimersStop(UUId_t TimerId);
OsStatus_t ScGetProcessBaseAddress(uintptr_t* BaseAddress);

///////////////////////////////////////////////
// Operating System Interface
// - Unprotected, all

// Threading system calls
UUId_t     ScThreadCreate(ThreadEntry_t Entry, void* Data, Flags_t Flags, UUId_t MemorySpaceHandle);
OsStatus_t ScThreadExit(int ExitCode);
OsStatus_t ScThreadJoin(UUId_t ThreadId, int* ExitCode);
OsStatus_t ScThreadDetach(UUId_t ThreadId);
OsStatus_t ScThreadSignal(UUId_t ThreadId, int SignalCode);
OsStatus_t ScThreadSleep(time_t Milliseconds, time_t* MillisecondsSlept);
UUId_t     ScThreadGetCurrentId(void);
OsStatus_t ScThreadYield(void);
OsStatus_t ScThreadSetCurrentName(const char* ThreadName);
OsStatus_t ScThreadGetCurrentName(char* ThreadNameBuffer, size_t MaxLength);

// Synchronization system calls
OsStatus_t ScConditionCreate(Handle_t* Handle);
OsStatus_t ScConditionDestroy(Handle_t Handle);
OsStatus_t ScSignalHandle(uintptr_t* Handle);
OsStatus_t ScSignalHandleAll(uintptr_t* Handle);
OsStatus_t ScWaitForObject(uintptr_t* Handle, size_t Timeout);

// Communication system calls
OsStatus_t ScCreatePipe(int Type, UUId_t* Handle);
OsStatus_t ScDestroyPipe(UUId_t Handle);
OsStatus_t ScReadPipe(UUId_t Handle, uint8_t* Message, size_t Length);
OsStatus_t ScWritePipe(UUId_t Handle, uint8_t* Message, size_t Length);
OsStatus_t ScRpcResponse(MRemoteCall_t* RemoteCall);
OsStatus_t ScRpcExecute(MRemoteCall_t* RemoteCall, int Async);
OsStatus_t ScRpcListen(MRemoteCall_t* RemoteCall, uint8_t* ArgumentBuffer);
OsStatus_t ScRpcRespond(MRemoteCallAddress_t* RemoteAddress, const uint8_t* Buffer, size_t Length);

// Memory system calls
OsStatus_t ScMemoryAllocate(size_t Size, Flags_t Flags, uintptr_t* VirtualAddress, uintptr_t* PhysicalAddress);
OsStatus_t ScMemoryFree(uintptr_t  Address, size_t Size);
OsStatus_t ScMemoryQuery(MemoryDescriptor_t *Descriptor);
OsStatus_t ScMemoryProtect(void* MemoryPointer, size_t Length, Flags_t Flags, Flags_t* PreviousFlags);
OsStatus_t ScCreateBuffer(size_t Size, DmaBuffer_t* MemoryBuffer);
OsStatus_t ScAcquireBuffer(UUId_t Handle, DmaBuffer_t* MemoryBuffer);
OsStatus_t ScQueryBuffer(UUId_t Handle, uintptr_t* Dma, size_t* Capacity);

// Support system calls
OsStatus_t ScDestroyHandle(UUId_t Handle);
OsStatus_t ScInstallSignalHandler(uintptr_t Handler);
OsStatus_t ScRaiseSignal(UUId_t ThreadHandle, int Signal);
OsStatus_t ScCreateMemoryHandler(Flags_t Flags, size_t Length, UUId_t* HandleOut, uintptr_t* AddressBaseOut);
OsStatus_t ScDestroyMemoryHandler(UUId_t Handle);
OsStatus_t ScFlushHardwareCache(int Cache, void* Start, size_t Length);
int        ScEnvironmentQuery(void);
OsStatus_t ScSystemTime(struct tm *SystemTime);
OsStatus_t ScSystemTick(int TickBase, clock_t *SystemTick);
OsStatus_t ScPerformanceFrequency(LargeInteger_t *Frequency);
OsStatus_t ScPerformanceTick(LargeInteger_t *Value);
OsStatus_t NoOperation(void) { return OsSuccess; }

// The static system calls function table.
uintptr_t GlbSyscallTable[79] = {
    ///////////////////////////////////////////////
    // Operating System Interface
    // - Protected, services/modules

    // System system calls
    DefineSyscall(0, ScSystemDebug),
    DefineSyscall(1, ScEndBootSequence),
    DefineSyscall(2, ScQueryDisplayInformation),
    DefineSyscall(3, ScCreateDisplayFramebuffer),

    // Module system calls
    DefineSyscall(4, ScModuleGetStartupInformation),
    DefineSyscall(5, ScModuleGetCurrentId),
    DefineSyscall(6, ScModuleGetCurrentName),
    DefineSyscall(7, ScModuleGetModuleHandles),
    DefineSyscall(8, ScModuleGetModuleEntryPoints),
    DefineSyscall(9, ScModuleExit),

    DefineSyscall(10, ScSharedObjectLoad),
    DefineSyscall(11, ScSharedObjectGetFunction),
    DefineSyscall(12, ScSharedObjectUnload),

    DefineSyscall(13, ScGetWorkingDirectory),
    DefineSyscall(14, ScSetWorkingDirectory),
    DefineSyscall(15, ScGetAssemblyDirectory),

    DefineSyscall(16, ScCreateMemorySpace),
    DefineSyscall(17, ScGetThreadMemorySpaceHandle),
    DefineSyscall(18, ScCreateMemorySpaceMapping),

    // Driver system calls
    DefineSyscall(19, ScAcpiQueryStatus),
    DefineSyscall(20, ScAcpiQueryTableHeader),
    DefineSyscall(21, ScAcpiQueryTable),
    DefineSyscall(22, ScAcpiQueryInterrupt),
    DefineSyscall(23, ScIoSpaceRegister),
    DefineSyscall(24, ScIoSpaceAcquire),
    DefineSyscall(25, ScIoSpaceRelease),
    DefineSyscall(26, ScIoSpaceDestroy),
    DefineSyscall(27, ScRegisterAliasId),
    DefineSyscall(28, ScLoadDriver),
    DefineSyscall(29, ScRegisterInterrupt),
    DefineSyscall(30, ScUnregisterInterrupt),
    DefineSyscall(31, ScRegisterEventTarget),
    DefineSyscall(32, ScKeyEvent),
    DefineSyscall(33, ScInputEvent),
    DefineSyscall(34, ScTimersStart),
    DefineSyscall(35, ScTimersStop),
    DefineSyscall(36, ScGetProcessBaseAddress),

    ///////////////////////////////////////////////
    // Operating System Interface
    // - Unprotected, all

    // Threading system calls
    DefineSyscall(37, ScThreadCreate),
    DefineSyscall(38, ScThreadExit),
    DefineSyscall(39, ScThreadSignal),
    DefineSyscall(40, ScThreadJoin),
    DefineSyscall(41, ScThreadDetach),
    DefineSyscall(42, ScThreadSleep),
    DefineSyscall(43, ScThreadYield),
    DefineSyscall(44, ScThreadGetCurrentId),
    DefineSyscall(45, ScThreadSetCurrentName),
    DefineSyscall(46, ScThreadGetCurrentName),

    // Synchronization system calls
    DefineSyscall(47, ScConditionCreate),
    DefineSyscall(48, ScConditionDestroy),
    DefineSyscall(49, ScWaitForObject),
    DefineSyscall(50, ScSignalHandle),
    DefineSyscall(51, ScSignalHandleAll),

    // Communication system calls
    DefineSyscall(52, ScCreatePipe),
    DefineSyscall(53, ScDestroyPipe),
    DefineSyscall(54, ScReadPipe),
    DefineSyscall(55, ScWritePipe),
    DefineSyscall(56, ScRpcExecute),
    DefineSyscall(57, ScRpcResponse),
    DefineSyscall(58, ScRpcListen),
    DefineSyscall(59, ScRpcRespond),

    // Memory system calls
    DefineSyscall(60, ScMemoryAllocate),
    DefineSyscall(61, ScMemoryFree),
    DefineSyscall(62, ScMemoryQuery),
    DefineSyscall(63, ScMemoryProtect),
    DefineSyscall(64, ScCreateBuffer),
    DefineSyscall(65, ScAcquireBuffer),
    DefineSyscall(66, ScQueryBuffer),

    // Support system calls
    DefineSyscall(67, ScDestroyHandle),
    DefineSyscall(68, ScInstallSignalHandler),
    DefineSyscall(69, ScRaiseSignal),
    DefineSyscall(70, ScCreateMemoryHandler),
    DefineSyscall(71, ScDestroyMemoryHandler),
    DefineSyscall(72, ScFlushHardwareCache),
    DefineSyscall(73, ScEnvironmentQuery),
    DefineSyscall(74, ScSystemTick),
    DefineSyscall(75, ScPerformanceFrequency),
    DefineSyscall(76, ScPerformanceTick),
    DefineSyscall(77, ScSystemTime),
    DefineSyscall(78, NoOperation)
};

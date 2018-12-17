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

struct FileMappingParameters;
struct MemoryMappingParameters;

///////////////////////////////////////////////
// Operating System Interface
// - Protected, services/modules

// System system calls
OsStatus_t ScSystemDebug(int Type, const char* Module, const char* Message);
OsStatus_t ScEndBootSequence(void);
OsStatus_t ScFlushHardwareCache(int Cache, void* Start, size_t Length);
int        ScEnvironmentQuery(void);
OsStatus_t ScSystemTime(struct tm *SystemTime);
OsStatus_t ScSystemTick(int TickBase, clock_t *SystemTick);
OsStatus_t ScPerformanceFrequency(LargeInteger_t *Frequency);
OsStatus_t ScPerformanceTick(LargeInteger_t *Value);
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
OsStatus_t NoOperation(void) { return OsSuccess; }

// The static system calls function table.
uintptr_t GlbSyscallTable[79] = {
    ///////////////////////////////////////////////
    // Operating System Interface
    // - Protected, services/modules

    // System system calls
    DefineSyscall(0, ScSystemDebug),
    DefineSyscall(1, ScEndBootSequence),
    DefineSyscall(2, ScFlushHardwareCache),
    DefineSyscall(3, ScEnvironmentQuery),
    DefineSyscall(4, ScSystemTick),
    DefineSyscall(5, ScPerformanceFrequency),
    DefineSyscall(6, ScPerformanceTick),
    DefineSyscall(7, ScSystemTime),
    DefineSyscall(8, ScQueryDisplayInformation),
    DefineSyscall(9, ScCreateDisplayFramebuffer),

    // Module system calls
    DefineSyscall(10, ScModuleGetStartupInformation),
    DefineSyscall(11, ScModuleGetCurrentId),
    DefineSyscall(12, ScModuleGetCurrentName),
    DefineSyscall(13, ScModuleGetModuleHandles),
    DefineSyscall(14, ScModuleGetModuleEntryPoints),
    DefineSyscall(15, ScModuleExit),

    DefineSyscall(16, ScSharedObjectLoad),
    DefineSyscall(17, ScSharedObjectGetFunction),
    DefineSyscall(18, ScSharedObjectUnload),

    DefineSyscall(19, ScGetWorkingDirectory),
    DefineSyscall(20, ScSetWorkingDirectory),
    DefineSyscall(21, ScGetAssemblyDirectory),

    DefineSyscall(22, ScCreateMemorySpace),
    DefineSyscall(23, ScGetThreadMemorySpaceHandle),
    DefineSyscall(24, ScCreateMemorySpaceMapping),

    // Driver system calls
    DefineSyscall(25, ScAcpiQueryStatus),
    DefineSyscall(26, ScAcpiQueryTableHeader),
    DefineSyscall(27, ScAcpiQueryTable),
    DefineSyscall(28, ScAcpiQueryInterrupt),
    DefineSyscall(29, ScIoSpaceRegister),
    DefineSyscall(30, ScIoSpaceAcquire),
    DefineSyscall(31, ScIoSpaceRelease),
    DefineSyscall(32, ScIoSpaceDestroy),
    DefineSyscall(33, ScRegisterAliasId),
    DefineSyscall(34, ScLoadDriver),
    DefineSyscall(35, ScRegisterInterrupt),
    DefineSyscall(36, ScUnregisterInterrupt),
    DefineSyscall(37, ScRegisterEventTarget),
    DefineSyscall(38, ScKeyEvent),
    DefineSyscall(39, ScInputEvent),
    DefineSyscall(40, ScTimersStart),
    DefineSyscall(41, ScTimersStop),

    ///////////////////////////////////////////////
    // Operating System Interface
    // - Unprotected, all

    // Threading system calls
    DefineSyscall(42, ScThreadCreate),
    DefineSyscall(43, ScThreadExit),
    DefineSyscall(44, ScThreadSignal),
    DefineSyscall(45, ScThreadJoin),
    DefineSyscall(46, ScThreadDetach),
    DefineSyscall(47, ScThreadSleep),
    DefineSyscall(48, ScThreadYield),
    DefineSyscall(49, ScThreadGetCurrentId),
    DefineSyscall(50, ScThreadSetCurrentName),
    DefineSyscall(51, ScThreadGetCurrentName),

    // Synchronization system calls
    DefineSyscall(52, ScConditionCreate),
    DefineSyscall(53, ScConditionDestroy),
    DefineSyscall(54, ScWaitForObject),
    DefineSyscall(55, ScSignalHandle),
    DefineSyscall(56, ScSignalHandleAll),

    // Communication system calls
    DefineSyscall(57, ScCreatePipe),
    DefineSyscall(58, ScDestroyPipe),
    DefineSyscall(59, ScReadPipe),
    DefineSyscall(60, ScWritePipe),
    DefineSyscall(61, ScRpcExecute),
    DefineSyscall(62, ScRpcResponse),
    DefineSyscall(63, ScRpcListen),
    DefineSyscall(64, ScRpcRespond),

    // Memory system calls
    DefineSyscall(65, ScMemoryAllocate),
    DefineSyscall(66, ScMemoryFree),
    DefineSyscall(67, ScMemoryQuery),
    DefineSyscall(68, ScMemoryProtect),
    DefineSyscall(69, ScCreateBuffer),
    DefineSyscall(70, ScAcquireBuffer),
    DefineSyscall(71, ScQueryBuffer),

    // Support system calls
    DefineSyscall(72, ScDestroyHandle),
    DefineSyscall(73, ScInstallSignalHandler),
    DefineSyscall(74, ScRaiseSignal),
    DefineSyscall(75, ScCreateMemoryHandler),
    DefineSyscall(76, ScDestroyMemoryHandler),
    DefineSyscall(78, NoOperation)
};

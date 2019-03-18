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

#include <ddk/contracts/video.h>
#include <os/mollenos.h>
#include <ddk/ipc/ipc.h>
#include <ddk/process.h>
#include <ddk/buffer.h>
#include <threading.h>
#include <ddk/acpi.h>
#include <os/input.h>
#include <time.h>

struct MemoryMappingParameters;

///////////////////////////////////////////////
// Operating System Interface
// - Protected, services/modules

// System system calls
extern OsStatus_t ScSystemDebug(int Type, const char* Module, const char* Message);
extern OsStatus_t ScEndBootSequence(void);
extern OsStatus_t ScQueryDisplayInformation(VideoDescriptor_t *Descriptor);
extern void*      ScCreateDisplayFramebuffer(void);

// Module system calls
extern OsStatus_t ScModuleGetStartupInformation(void* InheritanceBlock, size_t* InheritanceBlockLength, void* ArgumentBlock, size_t* ArgumentBlockLength);
extern OsStatus_t ScModuleGetCurrentId(UUId_t* Handle);
extern OsStatus_t ScModuleGetCurrentName(const char* Buffer, size_t MaxLength);
extern OsStatus_t ScModuleGetModuleHandles(Handle_t ModuleList[PROCESS_MAXMODULES]);
extern OsStatus_t ScModuleGetModuleEntryPoints(Handle_t ModuleList[PROCESS_MAXMODULES]);
extern OsStatus_t ScModuleExit(int ExitCode);

extern OsStatus_t ScSharedObjectLoad(const char* SoName, Handle_t* HandleOut);
extern uintptr_t  ScSharedObjectGetFunction(Handle_t Handle, const char* Function);
extern OsStatus_t ScSharedObjectUnload(Handle_t Handle);

extern OsStatus_t ScGetWorkingDirectory(char* PathBuffer, size_t MaxLength);
extern OsStatus_t ScSetWorkingDirectory(const char* Path);
extern OsStatus_t ScGetAssemblyDirectory(char* PathBuffer, size_t MaxLength);

extern OsStatus_t ScCreateMemorySpace(Flags_t Flags, UUId_t* Handle);
extern OsStatus_t ScGetThreadMemorySpaceHandle(UUId_t ThreadHandle, UUId_t* Handle);
extern OsStatus_t ScCreateMemorySpaceMapping(UUId_t Handle, struct MemoryMappingParameters* Parameters, void** AddressOut);

// Driver system calls
extern OsStatus_t ScAcpiQueryStatus(AcpiDescriptor_t* AcpiDescriptor);
extern OsStatus_t ScAcpiQueryTableHeader(const char* Signature, ACPI_TABLE_HEADER* Header);
extern OsStatus_t ScAcpiQueryTable(const char* Signature, ACPI_TABLE_HEADER* Table);
extern OsStatus_t ScAcpiQueryInterrupt(DevInfo_t Bus, DevInfo_t Device, int Pin, int* Interrupt, Flags_t* AcpiConform);
extern OsStatus_t ScIoSpaceRegister(DeviceIo_t* IoSpace);
extern OsStatus_t ScIoSpaceAcquire(DeviceIo_t* IoSpace);
extern OsStatus_t ScIoSpaceRelease(DeviceIo_t* IoSpace);
extern OsStatus_t ScIoSpaceDestroy(DeviceIo_t* IoSpace);
extern OsStatus_t ScRegisterAliasId(UUId_t Alias);
extern OsStatus_t ScLoadDriver(MCoreDevice_t* Device, size_t Length);
extern UUId_t     ScRegisterInterrupt(DeviceInterrupt_t* Interrupt, Flags_t Flags);
extern OsStatus_t ScUnregisterInterrupt(UUId_t Source);
extern OsStatus_t ScRegisterEventTarget(UUId_t KeyInput, UUId_t WmInput);
extern OsStatus_t ScKeyEvent(SystemKey_t* Key);
extern OsStatus_t ScInputEvent(SystemInput_t* Input);
extern OsStatus_t ScGetProcessBaseAddress(uintptr_t* BaseAddress);

///////////////////////////////////////////////
// Operating System Interface
// - Unprotected, all

// Threading system calls
extern OsStatus_t ScThreadCreate(ThreadEntry_t, void*, ThreadParameters_t*, UUId_t*);
extern OsStatus_t ScThreadExit(int ExitCode);
extern OsStatus_t ScThreadJoin(UUId_t ThreadId, int* ExitCode);
extern OsStatus_t ScThreadDetach(UUId_t ThreadId);
extern OsStatus_t ScThreadSignal(UUId_t ThreadId, int SignalCode);
extern OsStatus_t ScThreadSleep(time_t Milliseconds, time_t* MillisecondsSlept);
extern OsStatus_t ScThreadYield(void);
extern UUId_t     ScThreadGetCurrentId(void);
extern UUId_t     ScThreadCookie(void);
extern OsStatus_t ScThreadSetCurrentName(const char* ThreadName);
extern OsStatus_t ScThreadGetCurrentName(char* ThreadNameBuffer, size_t MaxLength);
extern OsStatus_t ScThreadGetContext(Context_t* ContextOut);

// Synchronization system calls
extern OsStatus_t ScConditionCreate(Handle_t* Handle);
extern OsStatus_t ScConditionDestroy(Handle_t Handle);
extern OsStatus_t ScSignalHandle(uintptr_t* Handle);
extern OsStatus_t ScSignalHandleAll(uintptr_t* Handle);
extern OsStatus_t ScWaitForObject(uintptr_t* Handle, size_t Timeout);

// Communication system calls
extern OsStatus_t ScCreatePipe(int Type, UUId_t* Handle);
extern OsStatus_t ScDestroyPipe(UUId_t Handle);
extern OsStatus_t ScReadPipe(UUId_t Handle, uint8_t* Message, size_t Length);
extern OsStatus_t ScWritePipe(UUId_t Handle, uint8_t* Message, size_t Length);
extern OsStatus_t ScRpcResponse(MRemoteCall_t* RemoteCall);
extern OsStatus_t ScRpcExecute(MRemoteCall_t* RemoteCall, int Async);
extern OsStatus_t ScRpcListen(MRemoteCall_t* RemoteCall, uint8_t* ArgumentBuffer);
extern OsStatus_t ScRpcRespond(MRemoteCallAddress_t* RemoteAddress, const uint8_t* Buffer, size_t Length);

// Memory system calls
extern OsStatus_t ScMemoryAllocate(size_t Size, Flags_t Flags, uintptr_t* VirtualAddress, uintptr_t* PhysicalAddress);
extern OsStatus_t ScMemoryFree(uintptr_t  Address, size_t Size);
extern OsStatus_t ScMemoryProtect(void* MemoryPointer, size_t Length, Flags_t Flags, Flags_t* PreviousFlags);
extern OsStatus_t ScCreateBuffer(size_t Size, DmaBuffer_t* MemoryBuffer);
extern OsStatus_t ScAcquireBuffer(UUId_t Handle, DmaBuffer_t* MemoryBuffer);
extern OsStatus_t ScQueryBuffer(UUId_t Handle, uintptr_t* Dma, size_t* Capacity);

// Support system calls
extern OsStatus_t ScDestroyHandle(UUId_t Handle);
extern OsStatus_t ScInstallSignalHandler(uintptr_t Handler);
extern OsStatus_t ScRaiseSignal(UUId_t ThreadHandle, int Signal);
extern OsStatus_t ScCreateMemoryHandler(Flags_t Flags, size_t Length, UUId_t* HandleOut, uintptr_t* AddressBaseOut);
extern OsStatus_t ScDestroyMemoryHandler(UUId_t Handle);
extern OsStatus_t ScFlushHardwareCache(int Cache, void* Start, size_t Length);
extern OsStatus_t ScSystemQuery(SystemDescriptor_t* Descriptor);
extern OsStatus_t ScSystemTime(SystemTime_t* SystemTime);
extern OsStatus_t ScSystemTick(int TickBase, LargeUInteger_t* Tick);
extern OsStatus_t ScPerformanceFrequency(LargeInteger_t *Frequency);
extern OsStatus_t ScPerformanceTick(LargeInteger_t *Value);
extern OsStatus_t ScIsServiceAvailable(UUId_t ServiceId);

// The static system calls function table.
uintptr_t GlbSyscallTable[77] = {
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
    DefineSyscall(34, ScGetProcessBaseAddress),

    ///////////////////////////////////////////////
    // Operating System Interface
    // - Unprotected, all

    // Threading system calls
    DefineSyscall(35, ScThreadCreate),
    DefineSyscall(36, ScThreadExit),
    DefineSyscall(37, ScThreadSignal),
    DefineSyscall(38, ScThreadJoin),
    DefineSyscall(39, ScThreadDetach),
    DefineSyscall(40, ScThreadSleep),
    DefineSyscall(41, ScThreadYield),
    DefineSyscall(42, ScThreadGetCurrentId),
    DefineSyscall(43, ScThreadCookie),
    DefineSyscall(44, ScThreadSetCurrentName),
    DefineSyscall(45, ScThreadGetCurrentName),
    DefineSyscall(46, ScThreadGetContext),

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
    DefineSyscall(62, ScMemoryProtect),
    DefineSyscall(63, ScCreateBuffer),
    DefineSyscall(64, ScAcquireBuffer),
    DefineSyscall(65, ScQueryBuffer),

    // Support system calls
    DefineSyscall(66, ScDestroyHandle),
    DefineSyscall(67, ScInstallSignalHandler),
    DefineSyscall(68, ScCreateMemoryHandler),
    DefineSyscall(69, ScDestroyMemoryHandler),
    DefineSyscall(70, ScFlushHardwareCache),
    DefineSyscall(71, ScSystemQuery),
    DefineSyscall(72, ScSystemTick),
    DefineSyscall(73, ScPerformanceFrequency),
    DefineSyscall(74, ScPerformanceTick),
    DefineSyscall(75, ScSystemTime),
    DefineSyscall(76, ScIsServiceAvailable)
};

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
#define DefineSyscall(_Sys) ((uintptr_t)&_Sys)

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
UUId_t     ScProcessSpawn(const char* Path, ProcessStartupInformation_t* StartupInformation);
OsStatus_t ScProcessJoin(UUId_t ProcessHandle, size_t Timeout, int* ExitCode);
OsStatus_t ScProcessKill(UUId_t ProcessHandle);
OsStatus_t ScProcessExit(int ExitCode);
OsStatus_t ScProcessGetCurrentId(UUId_t* ProcessHandle);
OsStatus_t ScProcessGetCurrentName(const char* Buffer, size_t MaxLength);
OsStatus_t ScProcessGetStartupInformation(ProcessStartupInformation_t* StartupInformation);
OsStatus_t ScProcessGetModuleHandles(Handle_t ModuleList[PROCESS_MAXMODULES]);
OsStatus_t ScProcessGetModuleEntryPoints(Handle_t ModuleList[PROCESS_MAXMODULES]);

OsStatus_t ScSharedObjectLoad(const char* SoName, uint8_t* Buffer, size_t BufferLength, Handle_t* HandleOut);
uintptr_t  ScSharedObjectGetFunction(Handle_t Handle, const char* Function);
OsStatus_t ScSharedObjectUnload(Handle_t Handle);

OsStatus_t ScGetWorkingDirectory(char* PathBuffer, size_t MaxLength);
OsStatus_t ScSetWorkingDirectory(const char* Path);
OsStatus_t ScGetAssemblyDirectory(char* PathBuffer, size_t MaxLength);

OsStatus_t ScCreateSystemMemorySpace(Flags_t Flags, UUId_t* Handle);
OsStatus_t ScGetThreadMemorySpaceHandle(UUId_t ThreadHandle, UUId_t* Handle);
OsStatus_t ScCreateSystemMemorySpaceMapping(UUId_t Handle, struct MemoryMappingParameters* Parameters, DmaBuffer_t* AccessBuffer);

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
OsStatus_t ScInstallSignalHandler(uintptr_t Handler);
OsStatus_t ScRaiseSignal(UUId_t ProcessHandle, int Signal);
OsStatus_t ScCreateFileMapping(struct FileMappingParameters* Parameters, void** MemoryPointer);
OsStatus_t ScDestroyFileMapping(void* MemoryPointer);
OsStatus_t ScDestroyHandle(UUId_t Handle);
OsStatus_t NoOperation(void) { return OsSuccess; }

// The static system calls function table.
uintptr_t   GlbSyscallTable[111] = {
    DefineSyscall(ScSystemDebug),

    /* Process & Threading
     * - Starting index is 1 */
    DefineSyscall(ScProcessExit),
    DefineSyscall(ScProcessGetCurrentId),
    DefineSyscall(ScProcessSpawn),
    DefineSyscall(ScProcessJoin),
    DefineSyscall(ScProcessKill),
    DefineSyscall(ScInstallSignalHandler),
    DefineSyscall(ScRaiseSignal),
    DefineSyscall(ScProcessGetCurrentName),
    DefineSyscall(ScProcessGetModuleHandles),
    DefineSyscall(ScProcessGetModuleEntryPoints),
    DefineSyscall(ScProcessGetStartupInformation),
    DefineSyscall(ScSharedObjectLoad),
    DefineSyscall(ScSharedObjectGetFunction),
    DefineSyscall(ScSharedObjectUnload),
    DefineSyscall(ScThreadCreate),
    DefineSyscall(ScThreadExit),
    DefineSyscall(ScThreadSignal),
    DefineSyscall(ScThreadJoin),
    DefineSyscall(ScThreadDetach),
    DefineSyscall(ScThreadSleep),
    DefineSyscall(ScThreadYield),
    DefineSyscall(ScThreadGetCurrentId),
    DefineSyscall(ScThreadSetCurrentName),
    DefineSyscall(ScThreadGetCurrentName),
    DefineSyscall(NoOperation),
    DefineSyscall(NoOperation),
    DefineSyscall(NoOperation),
    DefineSyscall(NoOperation),
    DefineSyscall(NoOperation),
    DefineSyscall(NoOperation),

    /* Synchronization Functions - 31 */
    DefineSyscall(ScConditionCreate),
    DefineSyscall(ScConditionDestroy),
    DefineSyscall(ScWaitForObject),
    DefineSyscall(ScSignalHandle),
    DefineSyscall(ScSignalHandleAll),
    DefineSyscall(NoOperation),
    DefineSyscall(NoOperation),
    DefineSyscall(NoOperation),
    DefineSyscall(NoOperation),
    DefineSyscall(NoOperation),

    /* Memory Functions - 41 */
    DefineSyscall(ScMemoryAllocate),
    DefineSyscall(ScMemoryFree),
    DefineSyscall(ScMemoryQuery),
    DefineSyscall(ScMemoryProtect),
    DefineSyscall(ScCreateBuffer),
    DefineSyscall(ScAcquireBuffer),
    DefineSyscall(ScQueryBuffer),
    DefineSyscall(ScCreateSystemMemorySpace),
    DefineSyscall(ScGetThreadMemorySpaceHandle),
    DefineSyscall(ScCreateSystemMemorySpaceMapping),

    /* Operating System Support Functions - 51 */
    DefineSyscall(ScGetWorkingDirectory),
    DefineSyscall(ScSetWorkingDirectory),
    DefineSyscall(ScGetAssemblyDirectory),
    DefineSyscall(ScCreateFileMapping),
    DefineSyscall(ScDestroyFileMapping),
    DefineSyscall(ScDestroyHandle),
    DefineSyscall(NoOperation),
    DefineSyscall(NoOperation),
    DefineSyscall(NoOperation),
    DefineSyscall(NoOperation),

    /* IPC Functions - 61 */
    DefineSyscall(ScCreatePipe),
    DefineSyscall(ScDestroyPipe),
    DefineSyscall(ScReadPipe),
    DefineSyscall(ScWritePipe),
    DefineSyscall(NoOperation),
    DefineSyscall(NoOperation),
    DefineSyscall(ScRpcExecute),
    DefineSyscall(ScRpcResponse),
    DefineSyscall(ScRpcListen),
    DefineSyscall(ScRpcRespond),

    /* System Functions - 71 */
    DefineSyscall(ScEndBootSequence),
    DefineSyscall(ScFlushHardwareCache),
    DefineSyscall(ScEnvironmentQuery),
    DefineSyscall(ScSystemTick),
    DefineSyscall(ScPerformanceFrequency),
    DefineSyscall(ScPerformanceTick),
    DefineSyscall(ScSystemTime),
    DefineSyscall(ScQueryDisplayInformation),
    DefineSyscall(ScCreateDisplayFramebuffer),
    DefineSyscall(NoOperation),

    /* Driver Functions - 81 
     * - ACPI Support */
    DefineSyscall(ScAcpiQueryStatus),
    DefineSyscall(ScAcpiQueryTableHeader),
    DefineSyscall(ScAcpiQueryTable),
    DefineSyscall(ScAcpiQueryInterrupt),
    DefineSyscall(NoOperation),
    DefineSyscall(NoOperation),
    DefineSyscall(NoOperation),
    DefineSyscall(NoOperation),
    DefineSyscall(NoOperation),
    DefineSyscall(NoOperation),

    /* Driver Functions - 91 
     * - I/O Support */
    DefineSyscall(ScIoSpaceRegister),
    DefineSyscall(ScIoSpaceAcquire),
    DefineSyscall(ScIoSpaceRelease),
    DefineSyscall(ScIoSpaceDestroy),

    /* Driver Functions - 95
     * - Support */
    DefineSyscall(ScRegisterAliasId),
    DefineSyscall(ScLoadDriver),
    DefineSyscall(ScRegisterEventTarget),
    DefineSyscall(NoOperation),
    DefineSyscall(NoOperation),
    DefineSyscall(NoOperation),

    /* Driver Functions - 101
     * - Interrupt Support */
    DefineSyscall(ScRegisterInterrupt),
    DefineSyscall(ScUnregisterInterrupt),
    DefineSyscall(ScKeyEvent),
    DefineSyscall(ScInputEvent),
    DefineSyscall(ScTimersStart),
    DefineSyscall(ScTimersStop),
    DefineSyscall(NoOperation),
    DefineSyscall(NoOperation),
    DefineSyscall(NoOperation),
    DefineSyscall(NoOperation)
};

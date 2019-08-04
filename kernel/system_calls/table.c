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
 * System Call Implementation
 *   - Table of calls
 */
#define DefineSyscall(Index, Fn) ((uintptr_t)&Fn)

#include <internal/_utils.h>
#include <ddk/contracts/video.h>
#include <os/mollenos.h>
#include <ddk/ipc/ipc.h>
#include <ddk/services/process.h>
#include <threading.h>
#include <ddk/acpi.h>
#include <os/input.h>
#include <threads.h>
#include <time.h>

struct MemoryMappingParameters;
struct dma_sg;
struct dma_buffer_info;
struct dma_attachment;

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
extern OsStatus_t ScFutexWait(FutexParameters_t* Parameters);
extern OsStatus_t ScFutexWake(FutexParameters_t* Parameters);

// Communication system calls
extern OsStatus_t ScIpcInvoke(UUId_t, IpcMessage_t*, unsigned int, size_t, void**);
extern OsStatus_t ScIpcGetResponse(size_t, void**);
extern OsStatus_t ScIpcListen(size_t, IpcMessage_t**);
extern OsStatus_t ScIpcReply(void*, size_t);
extern OsStatus_t ScIpcReplyAndListen(void*, size_t, size_t, IpcMessage_t**);

// Memory system calls
extern OsStatus_t ScMemoryAllocate(void*, size_t, Flags_t, void**);
extern OsStatus_t ScMemoryFree(uintptr_t  Address, size_t Size);
extern OsStatus_t ScMemoryProtect(void* MemoryPointer, size_t Length, Flags_t Flags, Flags_t* PreviousFlags);

extern OsStatus_t ScDmaCreate(struct dma_buffer_info*, struct dma_attachment*);
extern OsStatus_t ScDmaExport(void*, struct dma_buffer_info*, struct dma_attachment*);
extern OsStatus_t ScDmaAttach(UUId_t, struct dma_attachment*);
extern OsStatus_t ScDmaAttachmentMap(struct dma_attachment*);
extern OsStatus_t ScDmaAttachmentResize(struct dma_attachment*, size_t);
extern OsStatus_t ScDmaAttachmentRefresh(struct dma_attachment*);
extern OsStatus_t ScDmaAttachmentUnmap(struct dma_attachment*);
extern OsStatus_t ScDmaDetach(struct dma_attachment*);
extern OsStatus_t ScDmaGetMetrics(struct dma_attachment*, int*, struct dma_sg*);

// Support system calls
extern OsStatus_t ScDestroyHandle(UUId_t Handle);
extern OsStatus_t ScInstallSignalHandler(uintptr_t Handler);
extern OsStatus_t ScGetSignalOriginalContext(Context_t* Context);
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
uintptr_t SystemCallsTable[81] = {
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
    DefineSyscall(31, ScGetProcessBaseAddress),

    ///////////////////////////////////////////////
    // Operating System Interface
    // - Unprotected, all

    // Threading system calls
    DefineSyscall(32, ScThreadCreate),
    DefineSyscall(33, ScThreadExit),
    DefineSyscall(34, ScThreadSignal),
    DefineSyscall(35, ScThreadJoin),
    DefineSyscall(36, ScThreadDetach),
    DefineSyscall(37, ScThreadSleep),
    DefineSyscall(38, ScThreadYield),
    DefineSyscall(39, ScThreadGetCurrentId),
    DefineSyscall(40, ScThreadCookie),
    DefineSyscall(41, ScThreadSetCurrentName),
    DefineSyscall(42, ScThreadGetCurrentName),
    DefineSyscall(43, ScThreadGetContext),

    // Synchronization system calls
    DefineSyscall(44, ScFutexWait),
    DefineSyscall(45, ScFutexWake),

    // Communication system calls
    DefineSyscall(46, ScIpcInvoke),
    DefineSyscall(47, ScIpcGetResponse),
    DefineSyscall(48, ScIpcReply),
    DefineSyscall(49, ScIpcListen),
    DefineSyscall(50, ScIpcReplyAndListen),

    // Memory system calls
    DefineSyscall(51, ScMemoryAllocate),
    DefineSyscall(52, ScMemoryFree),
    DefineSyscall(53, ScMemoryProtect),
    
    DefineSyscall(54, ScDmaCreate),
    DefineSyscall(55, ScDmaExport),
    DefineSyscall(56, ScDmaAttach),
    DefineSyscall(57, ScDmaAttachmentMap),
    DefineSyscall(58, ScDmaAttachmentResize),
    DefineSyscall(59, ScDmaAttachmentRefresh),
    DefineSyscall(60, ScDmaAttachmentUnmap),
    DefineSyscall(61, ScDmaDetach),
    DefineSyscall(62, ScDmaGetMetrics),
    
    // Support system calls
    DefineSyscall(63, ScDestroyHandle),
    DefineSyscall(64, ScInstallSignalHandler),
    DefineSyscall(65, ScGetSignalOriginalContext),
    DefineSyscall(66, ScCreateMemoryHandler),
    DefineSyscall(67, ScDestroyMemoryHandler),
    DefineSyscall(68, ScFlushHardwareCache),
    DefineSyscall(69, ScSystemQuery),
    DefineSyscall(70, ScSystemTick),
    DefineSyscall(71, ScPerformanceFrequency),
    DefineSyscall(72, ScPerformanceTick),
    DefineSyscall(73, ScSystemTime),
    DefineSyscall(74, ScIsServiceAvailable)
};

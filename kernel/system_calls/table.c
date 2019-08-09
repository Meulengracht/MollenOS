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

#include <ddk/acpi.h>
#include <ddk/contracts/video.h>
#include <ddk/services/process.h>
#include <internal/_utils.h>
#include <os/mollenos.h>
#include <os/input.h>
#include <os/ipc.h>
#include <time.h>
#include <threading.h>
#include <threads.h>

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
extern OsStatus_t ScIpcReply(IpcMessage_t*, void*, size_t);
extern OsStatus_t ScIpcReplyAndListen(IpcMessage_t*, void*, size_t, size_t, IpcMessage_t**);

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
extern OsStatus_t ScRegisterHandlePath(UUId_t, const char*);
extern OsStatus_t ScLookupHandle(const char*, UUId_t*);
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
    DefineSyscall(27, ScLoadDriver),
    DefineSyscall(28, ScRegisterInterrupt),
    DefineSyscall(29, ScUnregisterInterrupt),
    DefineSyscall(30, ScGetProcessBaseAddress),

    ///////////////////////////////////////////////
    // Operating System Interface
    // - Unprotected, all

    // Threading system calls
    DefineSyscall(31, ScThreadCreate),
    DefineSyscall(32, ScThreadExit),
    DefineSyscall(33, ScThreadSignal),
    DefineSyscall(34, ScThreadJoin),
    DefineSyscall(35, ScThreadDetach),
    DefineSyscall(36, ScThreadSleep),
    DefineSyscall(37, ScThreadYield),
    DefineSyscall(38, ScThreadGetCurrentId),
    DefineSyscall(39, ScThreadCookie),
    DefineSyscall(40, ScThreadSetCurrentName),
    DefineSyscall(41, ScThreadGetCurrentName),
    DefineSyscall(42, ScThreadGetContext),

    // Synchronization system calls
    DefineSyscall(43, ScFutexWait),
    DefineSyscall(44, ScFutexWake),

    // Communication system calls
    DefineSyscall(45, ScIpcInvoke),
    DefineSyscall(46, ScIpcGetResponse),
    DefineSyscall(47, ScIpcReply),
    DefineSyscall(48, ScIpcListen),
    DefineSyscall(49, ScIpcReplyAndListen),

    // Memory system calls
    DefineSyscall(50, ScMemoryAllocate),
    DefineSyscall(51, ScMemoryFree),
    DefineSyscall(52, ScMemoryProtect),
    
    DefineSyscall(53, ScDmaCreate),
    DefineSyscall(54, ScDmaExport),
    DefineSyscall(55, ScDmaAttach),
    DefineSyscall(56, ScDmaAttachmentMap),
    DefineSyscall(57, ScDmaAttachmentResize),
    DefineSyscall(58, ScDmaAttachmentRefresh),
    DefineSyscall(59, ScDmaAttachmentUnmap),
    DefineSyscall(60, ScDmaDetach),
    DefineSyscall(61, ScDmaGetMetrics),
    
    // Support system calls
    DefineSyscall(62, ScRegisterHandlePath),
    DefineSyscall(63, ScLookupHandle),
    DefineSyscall(64, ScDestroyHandle),
    DefineSyscall(65, ScInstallSignalHandler),
    DefineSyscall(66, ScGetSignalOriginalContext),
    DefineSyscall(67, ScCreateMemoryHandler),
    DefineSyscall(68, ScDestroyMemoryHandler),
    DefineSyscall(69, ScFlushHardwareCache),
    DefineSyscall(70, ScSystemQuery),
    DefineSyscall(71, ScSystemTick),
    DefineSyscall(72, ScPerformanceFrequency),
    DefineSyscall(73, ScPerformanceTick),
    DefineSyscall(74, ScSystemTime)
};

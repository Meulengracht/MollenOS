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

#include <arch/utils.h>
#include <ddk/acpi.h>
#include <ddk/video.h>
#include <ddk/io.h>
#include <ddk/device.h>
#include <internal/_utils.h>
#include <ipc_context.h>
#include <os/types/process.h>
#include <os/mollenos.h>
#include <time.h>
#include <threading.h>
#include <threads.h>

DECL_STRUCT(DeviceInterrupt);

typedef struct handle_event handle_event_t;

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
extern OsStatus_t ScModuleGetStartupInformation(ProcessStartupInformation_t*, UUId_t*, char*, size_t);
extern OsStatus_t ScModuleGetCurrentName(const char* Buffer, size_t MaxLength);
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
extern OsStatus_t ScAcpiQueryInterrupt(unsigned int Bus, unsigned int Device, int Pin, int* Interrupt, Flags_t* AcpiConform);
extern OsStatus_t ScIoSpaceRegister(DeviceIo_t* IoSpace);
extern OsStatus_t ScIoSpaceAcquire(DeviceIo_t* IoSpace);
extern OsStatus_t ScIoSpaceRelease(DeviceIo_t* IoSpace);
extern OsStatus_t ScIoSpaceDestroy(DeviceIo_t* IoSpace);
extern OsStatus_t ScLoadDriver(Device_t* Device, size_t Length);
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
extern OsStatus_t ScDmaGetMetrics(UUId_t, int*, struct dma_sg*);

extern OsStatus_t ScCreateHandle(UUId_t*);
extern OsStatus_t ScDestroyHandle(UUId_t Handle);
extern OsStatus_t ScRegisterHandlePath(UUId_t, const char*);
extern OsStatus_t ScLookupHandle(const char*, UUId_t*);
extern OsStatus_t ScSetHandleActivity(UUId_t, Flags_t);

extern OsStatus_t ScCreateHandleSet(Flags_t, UUId_t*);
extern OsStatus_t ScControlHandleSet(UUId_t, int, UUId_t, Flags_t, void*);
extern OsStatus_t ScListenHandleSet(UUId_t, handle_event_t*, int, size_t, int*);

// Support system calls
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

#define SYSTEM_CALL_COUNT 74

typedef size_t(*SystemCallHandlerFn)(void*,void*,void*,void*,void*);

#define DefineSyscall(Index, Fn) { Index, ((uintptr_t)&Fn) }

// The static system calls function table.
static struct SystemCallDescriptor {
    int          Index;
    uintptr_t    HandlerAddress;
} SystemCallsTable[SYSTEM_CALL_COUNT] = {
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
    DefineSyscall(5, ScModuleGetCurrentName),
    DefineSyscall(6, ScModuleExit),

    DefineSyscall(7, ScSharedObjectLoad),
    DefineSyscall(8, ScSharedObjectGetFunction),
    DefineSyscall(9, ScSharedObjectUnload),

    DefineSyscall(10, ScGetWorkingDirectory),
    DefineSyscall(11, ScSetWorkingDirectory),
    DefineSyscall(12, ScGetAssemblyDirectory),

    DefineSyscall(13, ScCreateMemorySpace),
    DefineSyscall(14, ScGetThreadMemorySpaceHandle),
    DefineSyscall(15, ScCreateMemorySpaceMapping),

    // Driver system calls
    DefineSyscall(16, ScAcpiQueryStatus),
    DefineSyscall(17, ScAcpiQueryTableHeader),
    DefineSyscall(18, ScAcpiQueryTable),
    DefineSyscall(19, ScAcpiQueryInterrupt),
    DefineSyscall(20, ScIoSpaceRegister),
    DefineSyscall(21, ScIoSpaceAcquire),
    DefineSyscall(22, ScIoSpaceRelease),
    DefineSyscall(23, ScIoSpaceDestroy),
    DefineSyscall(24, ScLoadDriver),
    DefineSyscall(25, ScRegisterInterrupt),
    DefineSyscall(26, ScUnregisterInterrupt),
    DefineSyscall(27, ScGetProcessBaseAddress),

    ///////////////////////////////////////////////
    // Operating System Interface
    // - Unprotected, all

    // Threading system calls
    DefineSyscall(28, ScThreadCreate),
    DefineSyscall(29, ScThreadExit),
    DefineSyscall(30, ScThreadSignal),
    DefineSyscall(31, ScThreadJoin),
    DefineSyscall(32, ScThreadDetach),
    DefineSyscall(33, ScThreadSleep),
    DefineSyscall(34, ScThreadYield),
    DefineSyscall(35, ScThreadGetCurrentId),
    DefineSyscall(36, ScThreadCookie),
    DefineSyscall(37, ScThreadSetCurrentName),
    DefineSyscall(38, ScThreadGetCurrentName),
    DefineSyscall(39, ScThreadGetContext),

    // Synchronization system calls
    DefineSyscall(40, ScFutexWait),
    DefineSyscall(41, ScFutexWake),

    // Communication system calls
    DefineSyscall(42, IpcContextCreate),
    DefineSyscall(43, IpcContextSendMultiple),
    DefineSyscall(44, IpcContextRespondMultiple),

    // Memory system calls
    DefineSyscall(45, ScMemoryAllocate),
    DefineSyscall(46, ScMemoryFree),
    DefineSyscall(47, ScMemoryProtect),
    
    DefineSyscall(48, ScDmaCreate),
    DefineSyscall(49, ScDmaExport),
    DefineSyscall(50, ScDmaAttach),
    DefineSyscall(51, ScDmaAttachmentMap),
    DefineSyscall(52, ScDmaAttachmentResize),
    DefineSyscall(53, ScDmaAttachmentRefresh),
    DefineSyscall(54, ScDmaAttachmentUnmap),
    DefineSyscall(55, ScDmaDetach),
    DefineSyscall(56, ScDmaGetMetrics),
    
    DefineSyscall(57, ScCreateHandle),
    DefineSyscall(58, ScDestroyHandle),
    DefineSyscall(59, ScRegisterHandlePath),
    DefineSyscall(60, ScLookupHandle),
    DefineSyscall(61, ScSetHandleActivity),
    
    DefineSyscall(62, ScCreateHandleSet),
    DefineSyscall(63, ScControlHandleSet),
    DefineSyscall(64, ScListenHandleSet),
    
    // Support system calls
    DefineSyscall(65, ScInstallSignalHandler),
    DefineSyscall(66, ScCreateMemoryHandler),
    DefineSyscall(67, ScDestroyMemoryHandler),
    DefineSyscall(68, ScFlushHardwareCache),
    DefineSyscall(69, ScSystemQuery),
    DefineSyscall(70, ScSystemTick),
    DefineSyscall(71, ScPerformanceFrequency),
    DefineSyscall(72, ScPerformanceTick),
    DefineSyscall(73, ScSystemTime)
};

Context_t*
SyscallHandle(
    _In_ Context_t* Context)
{
    struct SystemCallDescriptor* Handler;
    MCoreThread_t*               Thread;
    size_t                       Index = CONTEXT_SC_FUNC(Context);
    size_t                       ReturnValue;
    
    if (Index > SYSTEM_CALL_COUNT) {
        CONTEXT_SC_RET0(Context) = (size_t)OsInvalidParameters;
        return Context;
    }
    
    Thread  = GetCurrentThreadForCore(ArchGetProcessorCoreId());
    Handler = &SystemCallsTable[Index];
    
    ReturnValue = ((SystemCallHandlerFn)Handler->HandlerAddress)(
        (void*)CONTEXT_SC_ARG0(Context), (void*)CONTEXT_SC_ARG1(Context),
        (void*)CONTEXT_SC_ARG2(Context), (void*)CONTEXT_SC_ARG3(Context),
        (void*)CONTEXT_SC_ARG4(Context));
    CONTEXT_SC_RET0(Context) = ReturnValue;
    
    // Before returning to userspace code, queue up any signals that might
    // have been queued up for us.
    SignalProcessQueued(Thread, Context);
    return Context;
}

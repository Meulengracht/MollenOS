/**
 * MollenOS
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

DECL_STRUCT(DeviceInterrupt);

struct MemoryMappingParameters;
struct dma_sg;
struct dma_buffer_info;
struct dma_attachment;

///////////////////////////////////////////////
// Operating System Interface
// - Protected, services/modules

// System system calls
extern OsStatus_t ScSystemDebug(int level, const char* message);
extern OsStatus_t ScEndBootSequence(void);
extern OsStatus_t ScQueryDisplayInformation(VideoDescriptor_t *Descriptor);
extern void*      ScCreateDisplayFramebuffer(void);

// Module system calls
extern OsStatus_t ScModuleGetStartupInformation(ProcessStartupInformation_t*, UUId_t*, char*, size_t);
extern OsStatus_t ScModuleGetCurrentName(const char* Buffer, size_t MaxLength);
extern OsStatus_t ScModuleExit(int ExitCode);

extern OsStatus_t ScSharedObjectLoad(const char*, Handle_t*, uintptr_t*);
extern uintptr_t  ScSharedObjectGetFunction(Handle_t Handle, const char* Function);
extern OsStatus_t ScSharedObjectUnload(Handle_t Handle);

extern OsStatus_t ScGetWorkingDirectory(char* PathBuffer, size_t MaxLength);
extern OsStatus_t ScSetWorkingDirectory(const char* Path);
extern OsStatus_t ScGetAssemblyDirectory(char* PathBuffer, size_t MaxLength);

extern OsStatus_t ScCreateMemorySpace(unsigned int Flags, UUId_t* Handle);
extern OsStatus_t ScGetThreadMemorySpaceHandle(UUId_t ThreadHandle, UUId_t* Handle);
extern OsStatus_t ScCreateMemorySpaceMapping(UUId_t Handle, struct MemoryMappingParameters* Parameters, void** AddressOut);

// Driver system calls
extern OsStatus_t ScAcpiQueryStatus(AcpiDescriptor_t* AcpiDescriptor);
extern OsStatus_t ScAcpiQueryTableHeader(const char* signature, ACPI_TABLE_HEADER* header);
extern OsStatus_t ScAcpiQueryTable(const char* signature, ACPI_TABLE_HEADER* table);
extern OsStatus_t ScAcpiQueryInterrupt(int, int, int, int*, unsigned int*);
extern OsStatus_t ScIoSpaceRegister(DeviceIo_t* IoSpace);
extern OsStatus_t ScIoSpaceAcquire(DeviceIo_t* IoSpace);
extern OsStatus_t ScIoSpaceRelease(DeviceIo_t* IoSpace);
extern OsStatus_t ScIoSpaceDestroy(DeviceIo_t* IoSpace);
extern OsStatus_t ScLoadDriver(Device_t* Device, size_t Length);
extern UUId_t     ScRegisterInterrupt(DeviceInterrupt_t* Interrupt, unsigned int Flags);
extern OsStatus_t ScUnregisterInterrupt(UUId_t Source);
extern OsStatus_t ScGetProcessBaseAddress(uintptr_t* BaseAddress);

extern OsStatus_t ScMapThreadMemoryRegion(UUId_t, uintptr_t, void**, void**);

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

// Synchronization system calls
extern OsStatus_t ScFutexWait(FutexParameters_t* parameters);
extern OsStatus_t ScFutexWake(FutexParameters_t* parameters);
extern OsStatus_t ScEventCreate(unsigned int, unsigned int, UUId_t*, atomic_int**);

// Memory system calls
extern OsStatus_t ScMemoryAllocate(void*, size_t, unsigned int, void**);
extern OsStatus_t ScMemoryFree(uintptr_t, size_t);
extern OsStatus_t ScMemoryProtect(void*, size_t, unsigned int, unsigned int*);
extern OsStatus_t ScMemoryQueryAllocation(void*, MemoryDescriptor_t*);
extern OsStatus_t ScMemoryQueryAttributes(void*, size_t, unsigned int*);

extern OsStatus_t ScDmaCreate(struct dma_buffer_info*, struct dma_attachment*);
extern OsStatus_t ScDmaExport(void*, struct dma_buffer_info*, struct dma_attachment*);
extern OsStatus_t ScDmaAttach(UUId_t, struct dma_attachment*);
extern OsStatus_t ScDmaAttachmentMap(struct dma_attachment*, unsigned int);
extern OsStatus_t ScDmaAttachmentResize(struct dma_attachment*, size_t);
extern OsStatus_t ScDmaAttachmentRefresh(struct dma_attachment*);
extern OsStatus_t ScDmaAttachmentCommit(struct dma_attachment*, void*, size_t);
extern OsStatus_t ScDmaAttachmentUnmap(struct dma_attachment*);
extern OsStatus_t ScDmaDetach(struct dma_attachment*);
extern OsStatus_t ScDmaGetMetrics(UUId_t, int*, struct dma_sg*);

extern OsStatus_t ScCreateHandle(UUId_t*);
extern OsStatus_t ScDestroyHandle(UUId_t Handle);
extern OsStatus_t ScRegisterHandlePath(UUId_t, const char*);
extern OsStatus_t ScLookupHandle(const char*, UUId_t*);
extern OsStatus_t ScSetHandleActivity(UUId_t, unsigned int);

extern OsStatus_t ScCreateHandleSet(unsigned int, UUId_t*);
extern OsStatus_t ScControlHandleSet(UUId_t, int, UUId_t, unsigned int, struct ioset_event*);
extern OsStatus_t ScListenHandleSet(UUId_t, HandleSetWaitParameters_t*, int*);

// Support system calls
extern OsStatus_t ScInstallSignalHandler(uintptr_t handler);
extern OsStatus_t ScFlushHardwareCache(int Cache, void* Start, size_t Length);
extern OsStatus_t ScSystemQuery(SystemDescriptor_t* Descriptor);
extern OsStatus_t ScSystemTime(SystemTime_t* systemTime);
extern OsStatus_t ScSystemTick(int tickBase, LargeUInteger_t* tick);
extern OsStatus_t ScPerformanceFrequency(LargeInteger_t *Frequency);
extern OsStatus_t ScPerformanceTick(LargeInteger_t *Value);

#define SYSTEM_CALL_COUNT 76

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

    DefineSyscall(28, ScMapThreadMemoryRegion),

    ///////////////////////////////////////////////
    // Operating System Interface
    // - Unprotected, all

    // Threading system calls
    DefineSyscall(29, ScThreadCreate),
    DefineSyscall(30, ScThreadExit),
    DefineSyscall(31, ScThreadSignal),
    DefineSyscall(32, ScThreadJoin),
    DefineSyscall(33, ScThreadDetach),
    DefineSyscall(34, ScThreadSleep),
    DefineSyscall(35, ScThreadYield),
    DefineSyscall(36, ScThreadGetCurrentId),
    DefineSyscall(37, ScThreadCookie),
    DefineSyscall(38, ScThreadSetCurrentName),
    DefineSyscall(39, ScThreadGetCurrentName),

    // Synchronization system calls
    DefineSyscall(40, ScFutexWait),
    DefineSyscall(41, ScFutexWake),
    DefineSyscall(42, ScEventCreate),

    // Communication system calls
    DefineSyscall(43, IpcContextCreate),
    DefineSyscall(44, IpcContextSendMultiple),
    DefineSyscall(45, IpcContextRespondMultiple),

    // Memory system calls
    DefineSyscall(46, ScMemoryAllocate),
    DefineSyscall(47, ScMemoryFree),
    DefineSyscall(48, ScMemoryProtect),
    DefineSyscall(49, ScMemoryQueryAllocation),
    DefineSyscall(50, ScMemoryQueryAttributes),
    
    DefineSyscall(51, ScDmaCreate),
    DefineSyscall(52, ScDmaExport),
    DefineSyscall(53, ScDmaAttach),
    DefineSyscall(54, ScDmaAttachmentMap),
    DefineSyscall(55, ScDmaAttachmentResize),
    DefineSyscall(56, ScDmaAttachmentRefresh),
    DefineSyscall(57, ScDmaAttachmentCommit),
    DefineSyscall(58, ScDmaAttachmentUnmap),
    DefineSyscall(59, ScDmaDetach),
    DefineSyscall(60, ScDmaGetMetrics),
    
    DefineSyscall(61, ScCreateHandle),
    DefineSyscall(62, ScDestroyHandle),
    DefineSyscall(63, ScRegisterHandlePath),
    DefineSyscall(64, ScLookupHandle),
    DefineSyscall(65, ScSetHandleActivity),

    DefineSyscall(66, ScCreateHandleSet),
    DefineSyscall(67, ScControlHandleSet),
    DefineSyscall(68, ScListenHandleSet),
    
    // Support system calls
    DefineSyscall(69, ScInstallSignalHandler),
    DefineSyscall(70, ScFlushHardwareCache),
    DefineSyscall(71, ScSystemQuery),
    DefineSyscall(72, ScSystemTick),
    DefineSyscall(73, ScPerformanceFrequency),
    DefineSyscall(74, ScPerformanceTick),
    DefineSyscall(75, ScSystemTime)
};

Context_t*
SyscallHandle(
    _In_ Context_t* context)
{
    struct SystemCallDescriptor* handler;
    Thread_t*                    thread;
    size_t                       index = CONTEXT_SC_FUNC(context);
    size_t                       returnValue;

    if (index >= SYSTEM_CALL_COUNT) {
        CONTEXT_SC_RET0(context) = (size_t)OsInvalidParameters;
        return context;
    }

    thread  = ThreadCurrentForCore(ArchGetProcessorCoreId());
    handler = &SystemCallsTable[index];

    returnValue = ((SystemCallHandlerFn)handler->HandlerAddress)(
            (void*)CONTEXT_SC_ARG0(context), (void*)CONTEXT_SC_ARG1(context),
            (void*)CONTEXT_SC_ARG2(context), (void*)CONTEXT_SC_ARG3(context),
            (void*)CONTEXT_SC_ARG4(context));
    CONTEXT_SC_RET0(context) = returnValue;
    
    // Before returning to userspace code, queue up any signals that might
    // have been queued up for us.
    SignalProcessQueued(thread, context);
    return context;
}

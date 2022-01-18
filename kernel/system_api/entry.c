/**
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
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 * System call interface
 */

//#define __TRACE

#include <arch/utils.h>
#include <ddk/acpi.h>
#include <ddk/video.h>
#include <ddk/io.h>
#include <debug.h>
#include <internal/_utils.h>
#include <ipc_context.h>
#include <os/mollenos.h>
#include <threading.h>

DECL_STRUCT(DeviceInterrupt);

struct MemoryMappingParameters;
struct dma_sg;
struct dma_buffer_info;
struct dma_attachment;

///////////////////////////////////////////////
// Operating System Interface
// - Protected, services/modules

// System specific system calls
extern OsStatus_t ScSystemDebug(int level, const char* message);
extern OsStatus_t ScEndBootSequence(void);
extern OsStatus_t ScQueryDisplayInformation(VideoDescriptor_t *Descriptor);
extern OsStatus_t ScMapBootFramebuffer(void** bufferOut);
extern OsStatus_t ScMapRamdisk(void** bufferOut, size_t* lengthOut);

// Module system calls
extern OsStatus_t ScCreateMemorySpace(unsigned int flags, UUId_t* handleOut);
extern OsStatus_t ScGetThreadMemorySpaceHandle(UUId_t threadHandle, UUId_t* handleOut);
extern OsStatus_t ScCreateMemorySpaceMapping(UUId_t handle, struct MemoryMappingParameters* mappingParameters, void** addressOut);

// Driver system calls
extern OsStatus_t ScAcpiQueryStatus(AcpiDescriptor_t* AcpiDescriptor);
extern OsStatus_t ScAcpiQueryTableHeader(const char* signature, ACPI_TABLE_HEADER* header);
extern OsStatus_t ScAcpiQueryTable(const char* signature, ACPI_TABLE_HEADER* table);
extern OsStatus_t ScAcpiQueryInterrupt(int, int, int, int*, unsigned int*);
extern OsStatus_t ScIoSpaceRegister(DeviceIo_t* ioSpace);
extern OsStatus_t ScIoSpaceAcquire(DeviceIo_t* IoSpace);
extern OsStatus_t ScIoSpaceRelease(DeviceIo_t* ioSpace);
extern OsStatus_t ScIoSpaceDestroy(DeviceIo_t* ioSpace);
extern UUId_t     ScRegisterInterrupt(DeviceInterrupt_t* deviceInterrupt, unsigned int flags);
extern OsStatus_t ScUnregisterInterrupt(UUId_t sourceId);
extern OsStatus_t ScGetProcessBaseAddress(uintptr_t* baseAddress);

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

// Misc interface
extern OsStatus_t ScInstallSignalHandler(uintptr_t handler);
extern OsStatus_t ScFlushHardwareCache(int Cache, void* Start, size_t Length);
extern OsStatus_t ScSystemQuery(SystemDescriptor_t*);

// Timing interface
extern OsStatus_t ScSystemClockTick(enum VaClockSourceType, LargeUInteger_t*);
extern OsStatus_t ScSystemClockFrequency(enum VaClockSourceType, LargeUInteger_t*);
extern OsStatus_t ScSystemWallClock(SystemTime_t*);
extern OsStatus_t ScTimeSleep(LargeUInteger_t*, LargeUInteger_t*);
extern OsStatus_t ScTimeStall(LargeUInteger_t*);

#define SYSTEM_CALL_COUNT 66

typedef size_t(*SystemCallHandlerFn)(void*,void*,void*,void*,void*);

#define DefineSyscall(Index, Fn) { Index, #Fn, ((uintptr_t)&(Fn)) }

// The static system calls function table.
static struct SystemCallDescriptor {
    int         Index;
    const char* Name;
    uintptr_t   HandlerAddress;
} SystemCallsTable[SYSTEM_CALL_COUNT] = {
        ///////////////////////////////////////////////
        // Operating System Interface
        // - Protected, services/modules

        // System specific system calls
        DefineSyscall(0, ScSystemDebug),
        DefineSyscall(1, ScEndBootSequence),
        DefineSyscall(2, ScQueryDisplayInformation),
        DefineSyscall(3, ScMapBootFramebuffer),
        DefineSyscall(4, ScMapRamdisk),

        DefineSyscall(5, ScCreateMemorySpace),
        DefineSyscall(6, ScGetThreadMemorySpaceHandle),
        DefineSyscall(7, ScCreateMemorySpaceMapping),

        // Driver system calls
        DefineSyscall(8, ScAcpiQueryStatus),
        DefineSyscall(9, ScAcpiQueryTableHeader),
        DefineSyscall(10, ScAcpiQueryTable),
        DefineSyscall(11, ScAcpiQueryInterrupt),
        DefineSyscall(12, ScIoSpaceRegister),
        DefineSyscall(13, ScIoSpaceAcquire),
        DefineSyscall(14, ScIoSpaceRelease),
        DefineSyscall(15, ScIoSpaceDestroy),
        DefineSyscall(16, ScRegisterInterrupt),
        DefineSyscall(17, ScUnregisterInterrupt),
        DefineSyscall(18, ScGetProcessBaseAddress),

        DefineSyscall(19, ScMapThreadMemoryRegion),

        ///////////////////////////////////////////////
        // Operating System Interface
        // - Unprotected, all

        // Threading interface
        DefineSyscall(20, ScThreadCreate),
        DefineSyscall(21, ScThreadExit),
        DefineSyscall(22, ScThreadSignal),
        DefineSyscall(23, ScThreadJoin),
        DefineSyscall(24, ScThreadDetach),
        DefineSyscall(25, ScThreadYield),
        DefineSyscall(26, ScThreadGetCurrentId),
        DefineSyscall(27, ScThreadCookie),
        DefineSyscall(28, ScThreadSetCurrentName),
        DefineSyscall(29, ScThreadGetCurrentName),

        // Synchronization interface
        DefineSyscall(30, ScFutexWait),
        DefineSyscall(31, ScFutexWake),
        DefineSyscall(32, ScEventCreate),

        // Communication interface
        DefineSyscall(33, IpcContextCreate),
        DefineSyscall(34, IpcContextSendMultiple),

        // Memory interface
        DefineSyscall(35, ScMemoryAllocate),
        DefineSyscall(36, ScMemoryFree),
        DefineSyscall(37, ScMemoryProtect),
        DefineSyscall(38, ScMemoryQueryAllocation),
        DefineSyscall(39, ScMemoryQueryAttributes),
    
        DefineSyscall(40, ScDmaCreate),
        DefineSyscall(41, ScDmaExport),
        DefineSyscall(42, ScDmaAttach),
        DefineSyscall(43, ScDmaAttachmentMap),
        DefineSyscall(44, ScDmaAttachmentResize),
        DefineSyscall(45, ScDmaAttachmentRefresh),
        DefineSyscall(46, ScDmaAttachmentCommit),
        DefineSyscall(47, ScDmaAttachmentUnmap),
        DefineSyscall(48, ScDmaDetach),
        DefineSyscall(49, ScDmaGetMetrics),
    
        DefineSyscall(50, ScCreateHandle),
        DefineSyscall(51, ScDestroyHandle),
        DefineSyscall(52, ScRegisterHandlePath),
        DefineSyscall(53, ScLookupHandle),
        DefineSyscall(54, ScSetHandleActivity),

        DefineSyscall(55, ScCreateHandleSet),
        DefineSyscall(56, ScControlHandleSet),
        DefineSyscall(57, ScListenHandleSet),
    
        // Misc interface
        DefineSyscall(58, ScInstallSignalHandler),
        DefineSyscall(59, ScFlushHardwareCache),
        DefineSyscall(60, ScSystemQuery),

        // Timing interface
        DefineSyscall(61, ScSystemClockTick),
        DefineSyscall(62, ScSystemClockFrequency),
        DefineSyscall(63, ScSystemWallClock),
        DefineSyscall(64, ScTimeSleep),
        DefineSyscall(65, ScTimeStall)
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

    TRACE("SyscallHandle %s", handler->Name);
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

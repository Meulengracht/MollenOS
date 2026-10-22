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

#include <assert.h>
#include <arch/thread.h>
#include <arch/utils.h>
#include <ddk/acpi.h>
#include <ddk/firmware.h>
#include <ddk/video.h>
#include <ddk/io.h>
#include <debug.h>
#include <handle_set.h>
#include <ipc_context.h>
#include <ddk/types/capabilities.h>
#include <os/futex.h>
#include <os/types/shm.h>
#include <os/types/thread.h>
#include <os/types/memory.h>
#include <os/types/time.h>
#include <os/types/syscall.h>
#include <threading.h>

DECL_STRUCT(InterruptGroupRequest);
DECL_STRUCT(InterruptGroupResult);
DECL_STRUCT(InterruptBindRequest);
DECL_STRUCT(DeviceInterruptMessage);

struct MemoryMappingParameters;

///////////////////////////////////////////////
// Operating System Interface
// - Protected, services/modules

// System specific system calls
extern oserr_t ScSystemDebug(enum OSSysLogLevel level, const char* message);
extern oserr_t ScMigrateKernelLog(void*, size_t, size_t*);
extern oserr_t ScMapBootFramebuffer(void** bufferOut);
extern oserr_t ScMapRamdisk(void** bufferOut, size_t* lengthOut);
extern oserr_t ScCapabilityGrant(Capabilities_t capabilities, uuid_t memorySpaceHandle);

// Phoenix system calls
extern oserr_t ScCreateMemorySpace(unsigned int flags, uuid_t* handleOut);
extern oserr_t ScGetThreadMemorySpaceHandle(uuid_t threadHandle, uuid_t* handleOut);
extern oserr_t ScCreateMemorySpaceMapping(uuid_t handle, struct MemoryMappingParameters* mappingParameters, void** addressOut);

// Driver system calls
extern oserr_t ScFirmwareQuery(OSFirmwareInfo_t* infoOut);
extern oserr_t ScFirmwareTableLocate(const OSFirmwareTableKey_t* key, OSFirmwareTable_t* tableOut);
extern oserr_t ScFirmwareTableRead(const OSFirmwareTableKey_t* key, void* buffer, size_t size, size_t* lengthOut);
extern oserr_t ScFirmwareTableMap(const OSFirmwareTableKey_t* key, const void** mappingOut, size_t* lengthOut);
extern oserr_t ScAcpiQueryInterrupt(int, int, int, int*, unsigned int*);
extern oserr_t ScIoSpaceRegister(DeviceIo_t* ioSpace);
extern oserr_t ScIoSpaceAcquire(DeviceIo_t* IoSpace);
extern oserr_t ScIoSpaceRelease(DeviceIo_t* ioSpace);
extern oserr_t ScIoSpaceDestroy(DeviceIo_t* ioSpace);
extern oserr_t ScIoSpaceMap(uuid_t, size_t, size_t, uintptr_t*);
extern oserr_t ScInterruptAllocate(InterruptGroupRequest_t*, InterruptGroupResult_t*);
extern oserr_t ScInterruptBind(uuid_t, InterruptBindRequest_t*);
extern oserr_t ScInterruptEnable(uuid_t);
extern oserr_t ScInterruptDisable(uuid_t);
extern oserr_t ScInterruptUnbind(uuid_t);
extern oserr_t ScInterruptRelease(uuid_t);
extern oserr_t ScInterruptGetMessage(uuid_t, uint32_t, DeviceInterruptMessage_t*);
extern oserr_t ScGetProcessBaseAddress(uintptr_t* baseAddress);

extern oserr_t ScMapThreadMemoryRegion(uuid_t, uintptr_t, void**, void**);

///////////////////////////////////////////////
// Operating System Interface
// - Unprotected, all

// Threading system calls
extern oserr_t ScThreadCreate(ThreadEntry_t, void*, OSThreadParameters_t*, uuid_t*);
extern oserr_t ScThreadExit(int ExitCode);
extern oserr_t ScThreadJoin(uuid_t ThreadId, int* ExitCode);
extern oserr_t ScThreadDetach(uuid_t ThreadId);
extern oserr_t ScThreadSignal(uuid_t ThreadId, int SignalCode);
extern oserr_t ScThreadYield(void);
extern uuid_t  ScThreadGetCurrentId(void);
extern uuid_t  ScThreadCookie(void);
extern oserr_t ScThreadSetCurrentName(const char* ThreadName);
extern oserr_t ScThreadGetCurrentName(char* ThreadNameBuffer, size_t MaxLength);

// Synchronization system calls
extern oserr_t ScFutexWait(OSAsyncContext_t*, OSFutexParameters_t*);
extern oserr_t ScFutexWake(OSFutexParameters_t*);
extern oserr_t ScEventCreate(unsigned int, unsigned int, uuid_t*, atomic_int**);

// Memory system calls
extern oserr_t ScMemoryAllocate(void*, size_t, unsigned int, void**);
extern oserr_t ScMemoryFree(uintptr_t, size_t);
extern oserr_t ScMemoryProtect(void*, size_t, unsigned int, unsigned int*);
extern oserr_t ScMemoryQueryAllocation(void*, OSMemoryDescriptor_t*);
extern oserr_t ScMemoryQueryAttributes(void*, size_t, unsigned int*);

extern oserr_t ScSHMCreate(SHM_t*, SHMHandle_t*);
extern oserr_t ScSHMExport(void*, SHM_t*, SHMHandle_t*);
extern oserr_t ScSHMConform(uuid_t, OSSHMConformParameters_t*, SHMHandle_t*);
extern oserr_t ScSHMAttach(uuid_t, SHMHandle_t*);
extern oserr_t ScSHMMap(SHMHandle_t*, size_t, size_t, unsigned int);
extern oserr_t ScSHMCommit(SHMHandle_t*, void*, size_t);
extern oserr_t ScSHMUnmap(SHMHandle_t*, void*, size_t);
extern oserr_t ScSHMDetach(SHMHandle_t*);
extern oserr_t ScSHMMetrics(uuid_t, int*, SHMSG_t*);

extern oserr_t ScCreateHandle(uuid_t*);
extern oserr_t ScDestroyHandle(uuid_t Handle);
extern oserr_t ScLookupHandle(const char*, uuid_t*);
extern oserr_t ScSetHandleActivity(uuid_t, unsigned int);

extern oserr_t ScCreateHandleSet(unsigned int, uuid_t*);
extern oserr_t ScControlHandleSet(uuid_t, int, uuid_t, struct ioset_event*);
extern oserr_t ScListenHandleSet(uuid_t, OSAsyncContext_t*, HandleSetWaitParameters_t*, int*);

// Misc interface
extern oserr_t ScInstallSignalHandler(uintptr_t handler);
extern oserr_t ScFlushHardwareCache(int Cache, void* Start, size_t Length);
extern oserr_t ScSystemQuery(enum OSSystemQueryRequest, void*, size_t, size_t*);

// Timing interface
extern oserr_t ScSystemClockTick(enum OSClockSource, UInteger64_t*);
extern oserr_t ScSystemClockFrequency(enum OSClockSource, UInteger64_t*);
extern oserr_t ScSystemTime(enum OSTimeSource, Integer64_t*);
extern oserr_t ScTimeSleep(OSTimestamp_t*, OSTimestamp_t*);
extern oserr_t ScTimeStall(UInteger64_t*);

extern oserr_t ScDmaOpen(uuid_t, const uint64_t*, uuid_t*);
extern oserr_t ScDmaAllocate(uuid_t, const struct SHMDeviceRequirements*, struct OSDmaAllocationResult*);
extern oserr_t ScDmaMapShm(uuid_t, uuid_t, const struct OSDmaMapParameters*, struct OSDmaMappingResult*);
extern oserr_t ScDmaGetSegments(uuid_t, struct SHMDeviceSegment*, uint32_t, uint32_t*);
extern oserr_t ScDmaSyncForDevice(uuid_t, size_t, size_t);
extern oserr_t ScDmaSyncForCpu(uuid_t, size_t, size_t);
extern oserr_t ScDmaUnmap(uuid_t);
extern oserr_t ScDmaClose(uuid_t);

#define SYSTEM_CALL_COUNT 78

typedef size_t(*SystemCallHandlerFn)(void*,void*,void*,void*,void*);

#define DefineSyscall(Index, Fn) { Index, #Fn, ((uintptr_t)&(Fn)) }

// The static system calls function table.
static struct SystemCallDescriptor {
    int         Index;
    const char* Name;
    uintptr_t   HandlerAddress;
} g_systemCallsTable[SYSTEM_CALL_COUNT] = {
        ///////////////////////////////////////////////
        // Operating System Interface
        // - Protected, services/modules

        // System specific system calls
        DefineSyscall(0, ScSystemDebug),
        DefineSyscall(1, ScMigrateKernelLog),
        DefineSyscall(2, ScMapBootFramebuffer),
        DefineSyscall(3, ScMapRamdisk),
        DefineSyscall(4, ScCapabilityGrant),

        DefineSyscall(5, ScCreateMemorySpace),
        DefineSyscall(6, ScGetThreadMemorySpaceHandle),
        DefineSyscall(7, ScCreateMemorySpaceMapping),

        // Driver system calls
        DefineSyscall(8, ScFirmwareQuery),
        DefineSyscall(9, ScFirmwareTableLocate),
        DefineSyscall(10, ScFirmwareTableRead),
        DefineSyscall(11, ScFirmwareTableMap),
        DefineSyscall(12, ScAcpiQueryInterrupt),
        DefineSyscall(13, ScIoSpaceRegister),
        DefineSyscall(14, ScIoSpaceAcquire),
        DefineSyscall(15, ScIoSpaceMap),
        DefineSyscall(16, ScIoSpaceRelease),
        DefineSyscall(17, ScIoSpaceDestroy),
        DefineSyscall(18, ScInterruptAllocate),
        DefineSyscall(19, ScInterruptBind),
        DefineSyscall(20, ScInterruptEnable),
        DefineSyscall(21, ScInterruptDisable),
        DefineSyscall(22, ScInterruptUnbind),
        DefineSyscall(23, ScInterruptRelease),
        DefineSyscall(24, ScInterruptGetMessage),
        DefineSyscall(25, ScGetProcessBaseAddress),

        DefineSyscall(26, ScMapThreadMemoryRegion),

        ///////////////////////////////////////////////
        // Operating System Interface
        // - Unprotected, all

        // Threading interface
        DefineSyscall(27, ScThreadCreate),
        DefineSyscall(28, ScThreadExit),
        DefineSyscall(29, ScThreadSignal),
        DefineSyscall(30, ScThreadJoin),
        DefineSyscall(31, ScThreadDetach),
        DefineSyscall(32, ScThreadYield),
        DefineSyscall(33, ScThreadGetCurrentId),
        DefineSyscall(34, ScThreadCookie),
        DefineSyscall(35, ScThreadSetCurrentName),
        DefineSyscall(36, ScThreadGetCurrentName),

        // Synchronization interface
        DefineSyscall(37, ScFutexWait),
        DefineSyscall(38, ScFutexWake),
        DefineSyscall(39, ScEventCreate),

        // Communication interface
        DefineSyscall(40, IpcContextSendMultiple),

        // Memory interface
        DefineSyscall(41, ScMemoryAllocate),
        DefineSyscall(42, ScMemoryFree),
        DefineSyscall(43, ScMemoryProtect),
        DefineSyscall(44, ScMemoryQueryAllocation),
        DefineSyscall(45, ScMemoryQueryAttributes),
    
        DefineSyscall(46, ScSHMCreate),
        DefineSyscall(47, ScSHMExport),
        DefineSyscall(48, ScSHMConform),
        DefineSyscall(49, ScSHMAttach),
        DefineSyscall(50, ScSHMMap),
        DefineSyscall(51, ScSHMCommit),
        DefineSyscall(52, ScSHMUnmap),
        DefineSyscall(53, ScSHMDetach),
        DefineSyscall(54, ScSHMMetrics),
    
        DefineSyscall(55, ScCreateHandle),
        DefineSyscall(56, ScDestroyHandle),
        DefineSyscall(57, ScLookupHandle),
        DefineSyscall(58, ScSetHandleActivity),

        DefineSyscall(59, ScCreateHandleSet),
        DefineSyscall(60, ScControlHandleSet),
        DefineSyscall(61, ScListenHandleSet),
    
        // Misc interface
        DefineSyscall(62, ScInstallSignalHandler),
        DefineSyscall(63, ScFlushHardwareCache),
        DefineSyscall(64, ScSystemQuery),

        // Timing interface
        DefineSyscall(65, ScSystemClockTick),
        DefineSyscall(66, ScSystemClockFrequency),
        DefineSyscall(67, ScSystemTime),
        DefineSyscall(68, ScTimeSleep),
        DefineSyscall(69, ScTimeStall),

        // DMA interface
        DefineSyscall(70, ScDmaOpen),
        DefineSyscall(71, ScDmaAllocate),
        DefineSyscall(72, ScDmaMapShm),
        DefineSyscall(73, ScDmaGetSegments),
        DefineSyscall(74, ScDmaSyncForDevice),
        DefineSyscall(75, ScDmaSyncForCpu),
        DefineSyscall(76, ScDmaUnmap),
        DefineSyscall(77, ScDmaClose)
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
        CONTEXT_SC_RET0(context) = (size_t)OS_EINVALPARAMS;
        return context;
    }

    handler = &g_systemCallsTable[index];

    TRACE("SyscallHandle %s", handler->Name);
    returnValue = ((SystemCallHandlerFn)handler->HandlerAddress)(
            (void*)CONTEXT_SC_ARG0(context), (void*)CONTEXT_SC_ARG1(context),
            (void*)CONTEXT_SC_ARG2(context), (void*)CONTEXT_SC_ARG3(context),
            (void*)CONTEXT_SC_ARG4(context));

    // Is the thread that is handling the system call a fork? Then the original
    // thread has already returned to userspace and this thread should notify
    // the main thread and then die peacefully. We also intentionally do not retrieve
    // the current thread before this point as we may be a different thread at exit
    // than we were on entry.
    thread = ThreadCurrentForCore(ArchGetProcessorCoreId());
    if (ThreadFlags(thread) & THREADING_FORKED) {
        OSAsyncContext_t* asyncContext = ThreadSyscallContext(thread);
        asyncContext->ErrorCode = (oserr_t)returnValue;
        (void)MarkHandle(asyncContext->NotificationHandle, 0x8);
        (void)ThreadTerminate(ThreadCurrentHandle(), 0, 1);
        ArchThreadYield();

        // catch all, the thread must not escape
        for (;;) { }
    }

    // Set the return code for the context before exitting the syscall handler
    CONTEXT_SC_RET0(context) = returnValue;

    // Before returning to userspace code, queue up any signals that might
    // have been queued up for us.
    SignalProcessQueued(thread, context);
    return context;
}

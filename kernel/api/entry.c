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
#include <ddk/types/capabilities.h>
#include <ddk/types/interrupt-controller.h>
#include <ddk/types/interrupt-group.h>
#include <ddk/acpi.h>
#include <ddk/firmware.h>
#include <ddk/video.h>
#include <ddk/io.h>
#include <os/futex.h>
#include <os/types/grants.h>
#include <os/types/shm.h>
#include <os/types/thread.h>
#include <os/types/memory.h>
#include <os/types/time.h>
#include <os/types/syscall.h>

#include <arch/thread.h>
#include <arch/utils.h>
#include <debug.h>
#include <handles/set.h>
#include <ipc_context.h>
#include <threading.h>

DECL_STRUCT(InterruptGroupRequest);
DECL_STRUCT(InterruptGroupResult);
DECL_STRUCT(InterruptBindRequest);
DECL_STRUCT(InterruptResourceTable);
DECL_STRUCT(InterruptMessageControllerIdentity);
DECL_STRUCT(DeviceInterruptMessage);
DECL_STRUCT(InterruptBusAttachRequest);

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
extern oserr_t ScCreateMemorySpace(unsigned int flags, MemorySpaceCreateResult_t* resultOut);
extern oserr_t ScGetThreadMemorySpaceHandle(uuid_t threadHandle, uuid_t* handleOut);
extern oserr_t ScCreateMemorySpaceMapping(uuid_t handle, struct MemoryMappingParameters* mappingParameters, void** addressOut);

// Driver system calls
extern oserr_t ScFirmwareQuery(OSFirmwareInfo_t* infoOut);
extern oserr_t ScFirmwareTableLocate(const OSFirmwareTableKey_t* key, OSFirmwareTable_t* tableOut);
extern oserr_t ScFirmwareTableRead(const OSFirmwareTableKey_t* key, void* buffer, size_t size, size_t* lengthOut);
extern oserr_t ScFirmwareTableMap(const OSFirmwareTableKey_t* key, const void** mappingOut, size_t* lengthOut);
extern oserr_t ScAcpiQueryPci(const void*, void*);

extern oserr_t ScIoSpaceRegister(DeviceIo_t* ioSpace);
extern oserr_t ScIoSpaceAcquire(DeviceIo_t* IoSpace);
extern oserr_t ScIoSpaceRelease(DeviceIo_t* ioSpace);
extern oserr_t ScIoSpaceDestroy(DeviceIo_t* ioSpace);
extern oserr_t ScIoSpaceMap(uuid_t, size_t, size_t, uintptr_t*);

extern oserr_t ScInterruptAllocate(InterruptGroupRequest_t*, InterruptGroupResult_t*);
extern oserr_t ScInterruptBind(uuid_t, InterruptBindRequest_t*);
extern oserr_t ScInterruptEnable(uuid_t);
extern oserr_t ScInterruptDisable(uuid_t);
extern oserr_t ScInterruptRelease(uuid_t);
extern oserr_t ScInterruptReap(uuid_t);

extern oserr_t ScInterruptBusHandlerCreate(uuid_t*);
extern oserr_t ScInterruptBusHandlerBind(uuid_t, const InterruptResourceTable_t*);
extern oserr_t ScInterruptBusHandlerRelease(uuid_t);
extern oserr_t ScInterruptBusAttach(const InterruptBusAttachRequest_t*);
extern oserr_t ScInterruptBusStatus(uuid_t, uuid_t, uuid_t);
extern oserr_t ScInterruptBusClose(uuid_t, uuid_t, uuid_t);

extern oserr_t ScInterruptGetMessage(uuid_t, uint32_t, DeviceInterruptMessage_t*);
extern oserr_t ScInterruptLineSourceResolve(const InterruptLineSourceIdentity_t*, uuid_t*);
extern oserr_t ScInterruptMessageControllerResolve(const InterruptMessageControllerIdentity_t*, uuid_t*);
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
extern oserr_t ScSHMAttach(uuid_t, SHMHandle_t*);
extern oserr_t ScSHMAttachToken(const GrantToken_t*, SHMHandle_t*);
extern oserr_t ScSHMMap(SHMHandle_t*, size_t, size_t, unsigned int);
extern oserr_t ScSHMCommit(SHMHandle_t*, void*, size_t);
extern oserr_t ScSHMUnmap(SHMHandle_t*, void*, size_t);
extern oserr_t ScSHMDetach(SHMHandle_t*);
extern oserr_t ScSHMMetrics(uuid_t, int*, SHMSG_t*);

extern oserr_t ScCreateHandle(uuid_t, uint32_t, uuid_t*);
extern oserr_t ScHandleRegisterProvider(uuid_t, uuid_t*);
extern oserr_t ScHandleImport(const GrantToken_t*, uuid_t, uuid_t*);
extern oserr_t ScHandleValidate(uuid_t, const uint64_t*, uuid_t, uint32_t, uuid_t*);
extern oserr_t ScHandleProviderGetEvents(uuid_t, uuid_t*, uint32_t, uint32_t*);
extern oserr_t ScDestroyHandle(uuid_t Handle);
extern oserr_t ScLookupHandle(const char*, uuid_t*);
extern oserr_t ScSetHandleActivity(uuid_t, unsigned int);

extern oserr_t ScGrantTokenCreate(uuid_t, const GrantTokenOptions_t*, GrantToken_t*);
extern oserr_t ScGrantTokenRevoke(uuid_t);

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

extern oserr_t ScDmaOpen(uuid_t, const struct OSDmaOpenParameters*, uuid_t*);
extern oserr_t ScDmaAllocate(uuid_t, const struct DMARequirements*, struct OSDmaAllocationResult*);
extern oserr_t ScDmaMapShm(uuid_t, uuid_t, const struct OSDmaMapParameters*, struct OSDmaMappingResult*);
extern oserr_t ScDmaGetSegments(uuid_t, struct DMASegment*, uint32_t, uint32_t*);
extern oserr_t ScResourceRelease(uuid_t);
extern oserr_t ScShmAccessEnd(uuid_t, enum SHMAccessKind, size_t);
extern oserr_t ScShmAccessBegin(OSAsyncContext_t*, uuid_t, const struct SHMAccessBeginParameters*);
extern oserr_t ScShmAccessCreate(uuid_t, size_t, size_t, uuid_t*);

// BEGIN GENERATED SYSCALL COUNT - tools/generate_syscalls.py
#define SYSTEM_CALL_COUNT 91
// END GENERATED SYSCALL COUNT

typedef size_t(*SystemCallHandlerFn)(void*,void*,void*,void*,void*);

#define DefineSyscall(Index, Fn) { Index, #Fn, ((uintptr_t)&(Fn)) }

// Syscall numbers and handlers are defined in syscalls.table.
static struct SystemCallDescriptor {
    int         Index;
    const char* Name;
    uintptr_t   HandlerAddress;
} g_systemCallsTable[SYSTEM_CALL_COUNT] = {
// BEGIN GENERATED SYSCALL TABLE - tools/generate_syscalls.py
    DefineSyscall(0, ScSystemDebug),
    DefineSyscall(1, ScMigrateKernelLog),
    DefineSyscall(2, ScMapBootFramebuffer),
    DefineSyscall(3, ScMapRamdisk),
    DefineSyscall(4, ScCapabilityGrant),
    DefineSyscall(5, ScCreateMemorySpace),
    DefineSyscall(6, ScGetThreadMemorySpaceHandle),
    DefineSyscall(7, ScCreateMemorySpaceMapping),
    DefineSyscall(8, ScFirmwareQuery),
    DefineSyscall(9, ScFirmwareTableLocate),
    DefineSyscall(10, ScFirmwareTableRead),
    DefineSyscall(11, ScFirmwareTableMap),
    DefineSyscall(12, ScAcpiQueryPci),
    DefineSyscall(13, ScIoSpaceRegister),
    DefineSyscall(14, ScIoSpaceAcquire),
    DefineSyscall(15, ScIoSpaceMap),
    DefineSyscall(16, ScIoSpaceRelease),
    DefineSyscall(17, ScIoSpaceDestroy),
    DefineSyscall(18, ScInterruptAllocate),
    DefineSyscall(19, ScInterruptBind),
    DefineSyscall(20, ScInterruptEnable),
    DefineSyscall(21, ScInterruptDisable),
    DefineSyscall(22, ScInterruptLineSourceResolve),
    DefineSyscall(23, ScInterruptRelease),
    DefineSyscall(24, ScInterruptGetMessage),
    DefineSyscall(25, ScInterruptReap),
    DefineSyscall(26, ScInterruptBusHandlerCreate),
    DefineSyscall(27, ScInterruptBusHandlerBind),
    DefineSyscall(28, ScInterruptBusHandlerRelease),
    DefineSyscall(29, ScInterruptBusAttach),
    DefineSyscall(30, ScInterruptBusStatus),
    DefineSyscall(31, ScInterruptBusClose),
    DefineSyscall(32, ScInterruptMessageControllerResolve),
    DefineSyscall(33, ScGetProcessBaseAddress),
    DefineSyscall(34, ScMapThreadMemoryRegion),
    DefineSyscall(35, ScThreadCreate),
    DefineSyscall(36, ScThreadExit),
    DefineSyscall(37, ScThreadSignal),
    DefineSyscall(38, ScThreadJoin),
    DefineSyscall(39, ScThreadDetach),
    DefineSyscall(40, ScThreadYield),
    DefineSyscall(41, ScThreadGetCurrentId),
    DefineSyscall(42, ScThreadCookie),
    DefineSyscall(43, ScThreadSetCurrentName),
    DefineSyscall(44, ScThreadGetCurrentName),
    DefineSyscall(45, ScFutexWait),
    DefineSyscall(46, ScFutexWake),
    DefineSyscall(47, ScEventCreate),
    DefineSyscall(48, IpcContextSendMultiple),
    DefineSyscall(49, ScMemoryAllocate),
    DefineSyscall(50, ScMemoryFree),
    DefineSyscall(51, ScMemoryProtect),
    DefineSyscall(52, ScMemoryQueryAllocation),
    DefineSyscall(53, ScMemoryQueryAttributes),
    DefineSyscall(54, ScSHMCreate),
    DefineSyscall(55, ScSHMAttachToken),
    DefineSyscall(56, ScSHMAttach),
    DefineSyscall(57, ScSHMMap),
    DefineSyscall(58, ScSHMCommit),
    DefineSyscall(59, ScSHMUnmap),
    DefineSyscall(60, ScSHMDetach),
    DefineSyscall(61, ScSHMMetrics),
    DefineSyscall(62, ScCreateHandle),
    DefineSyscall(63, ScHandleImport),
    DefineSyscall(64, ScDestroyHandle),
    DefineSyscall(65, ScLookupHandle),
    DefineSyscall(66, ScSetHandleActivity),
    DefineSyscall(67, ScHandleRegisterProvider),
    DefineSyscall(68, ScHandleValidate),
    DefineSyscall(69, ScHandleProviderGetEvents),
    DefineSyscall(70, ScGrantTokenCreate),
    DefineSyscall(71, ScGrantTokenRevoke),
    DefineSyscall(72, ScCreateHandleSet),
    DefineSyscall(73, ScControlHandleSet),
    DefineSyscall(74, ScListenHandleSet),
    DefineSyscall(75, ScInstallSignalHandler),
    DefineSyscall(76, ScFlushHardwareCache),
    DefineSyscall(77, ScSystemQuery),
    DefineSyscall(78, ScSystemClockTick),
    DefineSyscall(79, ScSystemClockFrequency),
    DefineSyscall(80, ScSystemTime),
    DefineSyscall(81, ScTimeSleep),
    DefineSyscall(82, ScTimeStall),
    DefineSyscall(83, ScDmaOpen),
    DefineSyscall(84, ScDmaAllocate),
    DefineSyscall(85, ScDmaMapShm),
    DefineSyscall(86, ScDmaGetSegments),
    DefineSyscall(87, ScShmAccessCreate),
    DefineSyscall(88, ScShmAccessBegin),
    DefineSyscall(89, ScShmAccessEnd),
    DefineSyscall(90, ScResourceRelease),
// END GENERATED SYSCALL TABLE
};

Context_t*
SyscallHandle(
    _In_ Context_t* context)
{
    struct SystemCallDescriptor* handler;
    Thread_t*                    thread;
    OSAsyncContext_t*            asyncContext;
    uuid_t                       notificationHandle;
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
        asyncContext = ThreadSyscallContext(thread);
        notificationHandle = asyncContext->NotificationHandle;

        // Publishing completion allows another execution unit to resume and
        // release the job's context. Do not read it after the release store.
        __atomic_store_n(&asyncContext->ErrorCode, (oserr_t)returnValue, __ATOMIC_RELEASE);
        (void)MarkHandle(notificationHandle, 0x8);
        ThreadExit(0);
    }

    // Set the return code for the context before exitting the syscall handler
    CONTEXT_SC_RET0(context) = returnValue;

    // Before returning to userspace code, queue up any signals that might
    // have been queued up for us.
    SignalProcessQueued(thread, context);
    return context;
}

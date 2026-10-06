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
#include <ddk/interrupt.h>
#include <debug.h>
#include <handle_set.h>
#include <ipc_context.h>
#include <os/futex.h>
#include <os/types/shm.h>
#include <os/types/thread.h>
#include <os/types/memory.h>
#include <os/types/time.h>
#include <os/types/syscall.h>
#include <threading.h>

DECL_STRUCT(DeviceInterrupt);

struct MemoryMappingParameters;

///////////////////////////////////////////////
// Operating System Interface
// - Protected, services/modules

// System specific system calls
extern oserr_t ScSystemDebug(enum OSSysLogLevel level, const char* message);
extern oserr_t ScMigrateKernelLog(void*, size_t, size_t*);
extern oserr_t ScMapBootFramebuffer(void** bufferOut);
extern oserr_t ScMapRamdisk(void** bufferOut, size_t* lengthOut);

// Module system calls
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
extern uuid_t  ScRegisterInterrupt(DeviceInterrupt_t* deviceInterrupt, unsigned int flags);
extern oserr_t ScUnregisterInterrupt(uuid_t sourceId);
extern oserr_t ScRegisterInterruptSet(DeviceInterrupt_t* interrupts, uint32_t count, unsigned int flags, uuid_t* setOut);
extern oserr_t ScDestroyInterruptSet(uuid_t setId);
extern oserr_t ScRegisterInterruptQuiesceEvent(uuid_t eventHandle);
extern oserr_t ScGetInterruptQuiesceRequest(DeviceInterruptQuiesceRequest_t* requestOut);
extern oserr_t ScCompleteInterruptQuiesce(uuid_t token);
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

#define SYSTEM_CALL_COUNT 68

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

        DefineSyscall(4, ScCreateMemorySpace),
        DefineSyscall(5, ScGetThreadMemorySpaceHandle),
        DefineSyscall(6, ScCreateMemorySpaceMapping),

        // Driver system calls
        DefineSyscall(7, ScFirmwareQuery),
        DefineSyscall(8, ScFirmwareTableLocate),
        DefineSyscall(9, ScFirmwareTableRead),
        DefineSyscall(10, ScFirmwareTableMap),
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
        DefineSyscall(33, IpcContextSendMultiple),

        // Memory interface
        DefineSyscall(34, ScMemoryAllocate),
        DefineSyscall(35, ScMemoryFree),
        DefineSyscall(36, ScMemoryProtect),
        DefineSyscall(37, ScMemoryQueryAllocation),
        DefineSyscall(38, ScMemoryQueryAttributes),
    
        DefineSyscall(39, ScSHMCreate),
        DefineSyscall(40, ScSHMExport),
        DefineSyscall(41, ScSHMConform),
        DefineSyscall(42, ScSHMAttach),
        DefineSyscall(43, ScSHMMap),
        DefineSyscall(44, ScSHMCommit),
        DefineSyscall(45, ScSHMUnmap),
        DefineSyscall(46, ScSHMDetach),
        DefineSyscall(47, ScSHMMetrics),
    
        DefineSyscall(48, ScCreateHandle),
        DefineSyscall(49, ScDestroyHandle),
        DefineSyscall(50, ScLookupHandle),
        DefineSyscall(51, ScSetHandleActivity),

        DefineSyscall(52, ScCreateHandleSet),
        DefineSyscall(53, ScControlHandleSet),
        DefineSyscall(54, ScListenHandleSet),
    
        // Misc interface
        DefineSyscall(55, ScInstallSignalHandler),
        DefineSyscall(56, ScFlushHardwareCache),
        DefineSyscall(57, ScSystemQuery),

        // Timing interface
        DefineSyscall(58, ScSystemClockTick),
        DefineSyscall(59, ScSystemClockFrequency),
        DefineSyscall(60, ScSystemTime),
        DefineSyscall(61, ScTimeSleep),
        DefineSyscall(62, ScTimeStall),
        DefineSyscall(63, ScRegisterInterruptSet),
        DefineSyscall(64, ScDestroyInterruptSet),
        DefineSyscall(65, ScRegisterInterruptQuiesceEvent),
        DefineSyscall(66, ScGetInterruptQuiesceRequest),
        DefineSyscall(67, ScCompleteInterruptQuiesce)
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

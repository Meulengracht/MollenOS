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
#include <ddk/video.h>
#include <ddk/io.h>
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
extern oserr_t ScAcpiQueryStatus(AcpiDescriptor_t* AcpiDescriptor);
extern oserr_t ScAcpiQueryTableHeader(const char* signature, ACPI_TABLE_HEADER* header);
extern oserr_t ScAcpiQueryTable(const char* signature, ACPI_TABLE_HEADER* table);
extern oserr_t ScAcpiQueryInterrupt(int, int, int, int*, unsigned int*);
extern oserr_t ScIoSpaceRegister(DeviceIo_t* ioSpace);
extern oserr_t ScIoSpaceAcquire(DeviceIo_t* IoSpace);
extern oserr_t ScIoSpaceRelease(DeviceIo_t* ioSpace);
extern oserr_t ScIoSpaceDestroy(DeviceIo_t* ioSpace);
extern uuid_t  ScRegisterInterrupt(DeviceInterrupt_t* deviceInterrupt, unsigned int flags);
extern oserr_t ScUnregisterInterrupt(uuid_t sourceId);
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
extern oserr_t ScSHMConform(uuid_t, enum OSMemoryConformity, unsigned int, SHMHandle_t*);
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

#define SYSTEM_CALL_COUNT 62

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
        DefineSyscall(7, ScAcpiQueryStatus),
        DefineSyscall(8, ScAcpiQueryTableHeader),
        DefineSyscall(9, ScAcpiQueryTable),
        DefineSyscall(10, ScAcpiQueryInterrupt),
        DefineSyscall(11, ScIoSpaceRegister),
        DefineSyscall(12, ScIoSpaceAcquire),
        DefineSyscall(13, ScIoSpaceRelease),
        DefineSyscall(14, ScIoSpaceDestroy),
        DefineSyscall(15, ScRegisterInterrupt),
        DefineSyscall(16, ScUnregisterInterrupt),
        DefineSyscall(17, ScGetProcessBaseAddress),

        DefineSyscall(18, ScMapThreadMemoryRegion),

        ///////////////////////////////////////////////
        // Operating System Interface
        // - Unprotected, all

        // Threading interface
        DefineSyscall(19, ScThreadCreate),
        DefineSyscall(20, ScThreadExit),
        DefineSyscall(21, ScThreadSignal),
        DefineSyscall(22, ScThreadJoin),
        DefineSyscall(23, ScThreadDetach),
        DefineSyscall(24, ScThreadYield),
        DefineSyscall(25, ScThreadGetCurrentId),
        DefineSyscall(26, ScThreadCookie),
        DefineSyscall(27, ScThreadSetCurrentName),
        DefineSyscall(28, ScThreadGetCurrentName),

        // Synchronization interface
        DefineSyscall(29, ScFutexWait),
        DefineSyscall(30, ScFutexWake),
        DefineSyscall(31, ScEventCreate),

        // Communication interface
        DefineSyscall(32, IpcContextSendMultiple),

        // Memory interface
        DefineSyscall(33, ScMemoryAllocate),
        DefineSyscall(34, ScMemoryFree),
        DefineSyscall(35, ScMemoryProtect),
        DefineSyscall(36, ScMemoryQueryAllocation),
        DefineSyscall(37, ScMemoryQueryAttributes),
    
        DefineSyscall(38, ScSHMCreate),
        DefineSyscall(39, ScSHMExport),
        DefineSyscall(40, ScSHMConform),
        DefineSyscall(41, ScSHMAttach),
        DefineSyscall(42, ScSHMMap),
        DefineSyscall(43, ScSHMCommit),
        DefineSyscall(44, ScSHMUnmap),
        DefineSyscall(45, ScSHMDetach),
        DefineSyscall(46, ScSHMMetrics),
    
        DefineSyscall(47, ScCreateHandle),
        DefineSyscall(48, ScDestroyHandle),
        DefineSyscall(49, ScLookupHandle),
        DefineSyscall(50, ScSetHandleActivity),

        DefineSyscall(51, ScCreateHandleSet),
        DefineSyscall(52, ScControlHandleSet),
        DefineSyscall(53, ScListenHandleSet),
    
        // Misc interface
        DefineSyscall(54, ScInstallSignalHandler),
        DefineSyscall(55, ScFlushHardwareCache),
        DefineSyscall(56, ScSystemQuery),

        // Timing interface
        DefineSyscall(57, ScSystemClockTick),
        DefineSyscall(58, ScSystemClockFrequency),
        DefineSyscall(59, ScSystemTime),
        DefineSyscall(60, ScTimeSleep),
        DefineSyscall(61, ScTimeStall)
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

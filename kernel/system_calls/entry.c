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

DECL_STRUCT(DeviceInterrupt);

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

extern OsStatus_t ScCreateMemorySpace(unsigned int Flags, UUId_t* Handle);
extern OsStatus_t ScGetThreadMemorySpaceHandle(UUId_t ThreadHandle, UUId_t* Handle);
extern OsStatus_t ScCreateMemorySpaceMapping(UUId_t Handle, struct MemoryMappingParameters* Parameters, void** AddressOut);

// Driver system calls
extern OsStatus_t ScAcpiQueryStatus(AcpiDescriptor_t* AcpiDescriptor);
extern OsStatus_t ScAcpiQueryTableHeader(const char* Signature, ACPI_TABLE_HEADER* Header);
extern OsStatus_t ScAcpiQueryTable(const char* Signature, ACPI_TABLE_HEADER* Table);
extern OsStatus_t ScAcpiQueryInterrupt(unsigned int Bus, unsigned int Device, int Pin, int* Interrupt, unsigned int* AcpiConform);
extern OsStatus_t ScIoSpaceRegister(DeviceIo_t* IoSpace);
extern OsStatus_t ScIoSpaceAcquire(DeviceIo_t* IoSpace);
extern OsStatus_t ScIoSpaceRelease(DeviceIo_t* IoSpace);
extern OsStatus_t ScIoSpaceDestroy(DeviceIo_t* IoSpace);
extern OsStatus_t ScLoadDriver(Device_t* Device, size_t Length);
extern UUId_t     ScRegisterInterrupt(DeviceInterrupt_t* Interrupt, unsigned int Flags);
extern OsStatus_t ScUnregisterInterrupt(UUId_t Source);
extern OsStatus_t ScGetProcessBaseAddress(uintptr_t* BaseAddress);

extern OsStatus_t ScMapThreadMemoryRegion(UUId_t, uintptr_t, size_t, void**);

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
extern OsStatus_t ScFutexWait(FutexParameters_t* parameters);
extern OsStatus_t ScFutexWake(FutexParameters_t* parameters);
extern OsStatus_t ScEventCreate(unsigned int, unsigned int, UUId_t*, atomic_int**);

// Memory system calls
extern OsStatus_t ScMemoryAllocate(void*, size_t, unsigned int, void**);
extern OsStatus_t ScMemoryFree(uintptr_t  Address, size_t Size);
extern OsStatus_t ScMemoryProtect(void* MemoryPointer, size_t Length, unsigned int Flags, unsigned int* PreviousFlags);

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
extern OsStatus_t ScSetHandleActivity(UUId_t, unsigned int);

extern OsStatus_t ScCreateHandleSet(unsigned int, UUId_t*);
extern OsStatus_t ScControlHandleSet(UUId_t, int, UUId_t, unsigned int, void*);
extern OsStatus_t ScListenHandleSet(UUId_t, HandleSetWaitParameters_t*, int*);

// Support system calls
extern OsStatus_t ScInstallSignalHandler(uintptr_t Handler);
extern OsStatus_t ScCreateMemoryHandler(unsigned int Flags, size_t Length, UUId_t* HandleOut, uintptr_t* AddressBaseOut);
extern OsStatus_t ScDestroyMemoryHandler(UUId_t Handle);
extern OsStatus_t ScFlushHardwareCache(int Cache, void* Start, size_t Length);
extern OsStatus_t ScSystemQuery(SystemDescriptor_t* Descriptor);
extern OsStatus_t ScSystemTime(SystemTime_t* SystemTime);
extern OsStatus_t ScSystemTick(int TickBase, LargeUInteger_t* Tick);
extern OsStatus_t ScPerformanceFrequency(LargeInteger_t *Frequency);
extern OsStatus_t ScPerformanceTick(LargeInteger_t *Value);

#define SYSTEM_CALL_COUNT 75

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
    
    DefineSyscall(49, ScDmaCreate),
    DefineSyscall(50, ScDmaExport),
    DefineSyscall(51, ScDmaAttach),
    DefineSyscall(52, ScDmaAttachmentMap),
    DefineSyscall(53, ScDmaAttachmentResize),
    DefineSyscall(54, ScDmaAttachmentRefresh),
    DefineSyscall(55, ScDmaAttachmentUnmap),
    DefineSyscall(56, ScDmaDetach),
    DefineSyscall(57, ScDmaGetMetrics),
    
    DefineSyscall(58, ScCreateHandle),
    DefineSyscall(59, ScDestroyHandle),
    DefineSyscall(60, ScRegisterHandlePath),
    DefineSyscall(61, ScLookupHandle),
    DefineSyscall(62, ScSetHandleActivity),

    DefineSyscall(63, ScCreateHandleSet),
    DefineSyscall(64, ScControlHandleSet),
    DefineSyscall(65, ScListenHandleSet),
    
    // Support system calls
    DefineSyscall(66, ScInstallSignalHandler),
    DefineSyscall(67, ScCreateMemoryHandler),
    DefineSyscall(68, ScDestroyMemoryHandler),
    DefineSyscall(69, ScFlushHardwareCache),
    DefineSyscall(70, ScSystemQuery),
    DefineSyscall(71, ScSystemTick),
    DefineSyscall(72, ScPerformanceFrequency),
    DefineSyscall(73, ScPerformanceTick),
    DefineSyscall(74, ScSystemTime)
};

Context_t*
SyscallHandle(
    _In_ Context_t* Context)
{
    struct SystemCallDescriptor* Handler;
    Thread_t*               Thread;
    size_t                       Index = CONTEXT_SC_FUNC(Context);
    size_t                       ReturnValue;
    
    if (Index > SYSTEM_CALL_COUNT) {
        CONTEXT_SC_RET0(Context) = (size_t)OsInvalidParameters;
        return Context;
    }
    
    Thread  = ThreadCurrentForCore(ArchGetProcessorCoreId());
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

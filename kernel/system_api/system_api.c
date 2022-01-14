/**
 * Copyright 2017, Philip Meulengracht
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
 * System Calls
 */

#define __MODULE "SCIF"
//#define __TRACE

#include <arch/output.h>
#include <arch/utils.h>
#include <component/timer.h>
#include <os/mollenos.h>
#include <memoryspace.h>
#include <threading.h>
#include <console.h>
#include <machine.h>
#include <debug.h>
#include <string.h>

OsStatus_t
ScSystemDebug(
    _In_ int         level,
    _In_ const char* message)
{
    if (message == NULL) {
        return OsInvalidParameters;
    }

    // Switch based on type
    if (level == 0) {
        LogAppendMessage(LOG_TRACE, message);
    }
    else if (level == 1) {
        LogAppendMessage(LOG_WARNING, message);
    }
    else {
        LogAppendMessage(LOG_ERROR, message);
    }
    return OsSuccess;
}


OsStatus_t ScEndBootSequence(void) {
    TRACE("Ending console session");
    LogSetRenderMode(0);
    return OsSuccess;
}

OsStatus_t
ScSystemQuery(
    _In_ SystemDescriptor_t* Descriptor)
{
    size_t maxBlocks  = GetMachine()->NumberOfMemoryBlocks;
    size_t freeBlocks = GetMachine()->NumberOfFreeMemoryBlocks;
    
    Descriptor->NumberOfProcessors  = atomic_load(&GetMachine()->NumberOfProcessors);
    Descriptor->NumberOfActiveCores = atomic_load(&GetMachine()->NumberOfActiveCores);

    Descriptor->AllocationGranularityBytes = GetMachine()->MemoryGranularity;
    Descriptor->PageSizeBytes = GetMemorySpacePageSize();
    Descriptor->PagesTotal = maxBlocks;
    Descriptor->PagesUsed  = maxBlocks - freeBlocks;
    return OsSuccess;
}

OsStatus_t
ScFlushHardwareCache(
    _In_     int    Cache,
    _In_Opt_ void*  Start, 
    _In_Opt_ size_t Length)
{
    if (Cache == CACHE_INSTRUCTION) {
        CpuFlushInstructionCache(Start, Length);
        return OsSuccess;
    }
    else if (Cache == CACHE_MEMORY) {
        CpuInvalidateMemoryCache(Start, Length);
        return OsSuccess;
    }
    return OsError;
}

OsStatus_t
ScSystemClockTick(
    _In_ enum VaClockSourceType source,
    _In_ LargeUInteger_t*       tickOut)
{
    if (!tickOut) {
        return OsInvalidParameters;
    }

    switch (source) {
        case VaClockSourceType_HPC:
            return SystemTimerGetPerformanceTick(tickOut);

        case VaClockSourceType_THREAD:
            SystemTimerGetClockTick(tickOut);
            Thread_t* Thread = ThreadCurrentForCore(ArchGetProcessorCoreId());
            if (Thread != NULL) {
                tickOut->QuadPart -= ThreadStartTime(Thread)->QuadPart;
            }
            break;

        default:
            SystemTimerGetClockTick(tickOut);
            break;
    }
    return OsSuccess;
}

OsStatus_t
ScSystemClockFrequency(
        _In_ enum VaClockSourceType source,
        _In_ LargeUInteger_t*       frequencyOut)
{
    if (!frequencyOut) {
        return OsInvalidParameters;
    }

    if (source == VaClockSourceType_HPC) {
        return SystemTimerGetPerformanceFrequency(frequencyOut);
    }

    SystemTimerGetClockFrequency(frequencyOut);
    return OsSuccess;
}

OsStatus_t
ScSystemWallClock(
        _In_ SystemTime_t* wallClock)
{
    if (wallClock == NULL) {
        return OsError;
    }
    memcpy(wallClock, &GetMachine()->SystemTimers.WallClock, sizeof(SystemTime_t));
    return OsSuccess;
}

OsStatus_t
ScQueryDisplayInformation(
    _In_ VideoDescriptor_t *Descriptor) {
    if (Descriptor == NULL) {
        return OsError;
    }
    return VideoQuery(Descriptor);
}

OsStatus_t
ScMapBootFramebuffer(
        _Out_ void** bufferOut)
{
    MemorySpace_t* memorySpace     = GetCurrentMemorySpace();
    uintptr_t      addressPhysical = VideoGetTerminal()->FrameBufferAddressPhysical;
    uintptr_t      fbVirtual       = 0;
    size_t         fbSize          = VideoGetTerminal()->Info.BytesPerScanline * VideoGetTerminal()->Info.Height;
    OsStatus_t     osStatus;

    if (!VideoGetTerminal()->FrameBufferAddressPhysical) {
        return OsNotSupported;
    }

    osStatus = MemorySpaceMapContiguous(
            memorySpace,
            &fbVirtual,
            addressPhysical,
            fbSize,
            MAPPING_COMMIT | MAPPING_USERSPACE | MAPPING_NOCACHE | MAPPING_PERSISTENT,
            MAPPING_VIRTUAL_PROCESS
    );

    if (osStatus == OsSuccess) {
        *bufferOut = (void*)fbVirtual;
    }
    return osStatus;
}

OsStatus_t
ScMapRamdisk(
        _Out_ void**  bufferOut,
        _Out_ size_t* lengthOut)
{
    OsStatus_t osStatus;
    vaddr_t    mapping;

    osStatus = MemorySpaceCloneMapping(
            GetCurrentMemorySpace(),
            GetCurrentMemorySpace(),
            (vaddr_t)GetMachine()->BootInformation.Ramdisk.Data,
            &mapping,
            GetMachine()->BootInformation.Ramdisk.Length,
            MAPPING_COMMIT | MAPPING_USERSPACE | MAPPING_READONLY,
            MAPPING_VIRTUAL_PROCESS
    );
    if (osStatus == OsSuccess) {
        *bufferOut = (void*)mapping;
        *lengthOut = GetMachine()->BootInformation.Ramdisk.Length;
    }
    return osStatus;
}

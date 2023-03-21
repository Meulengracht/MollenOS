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
#include <memoryspace.h>
#include <threading.h>
#include <console.h>
#include <machine.h>
#include <debug.h>

oserr_t
ScSystemDebug(
    _In_ enum OSSysLogLevel level,
    _In_ const char*        message)
{
    LogAppendMessage(level, message);
    return OS_EOK;
}

oserr_t
ScMigrateKernelLog(
        _In_ void*   buffer,
        _In_ size_t  bufferSize,
        _In_ size_t* bytesRead)
{
    return LogMigrate(buffer, bufferSize, bytesRead);
}

oserr_t
ScSystemQuery(
        _In_  enum OSSystemQueryRequest request,
        _In_  void*                     buffer,
        _In_  size_t                    bufferSize,
        _Out_ size_t*                   bytesQueriedOut)
{
    switch (request) {
        case OSSYSTEMQUERY_BOOTVIDEOINFO: {
            OSBootVideoDescriptor_t* info = buffer;
            oserr_t                  oserr;
            if (bufferSize < sizeof(OSBootVideoDescriptor_t)) {
                return OS_EINVALPARAMS;
            }
            oserr = VideoQuery(info);
            if (oserr != OS_EOK) {
                return oserr;
            }
            *bytesQueriedOut = sizeof(OSBootVideoDescriptor_t);
            return OS_EOK;
        } break;
        case OSSYSTEMQUERY_CPUINFO: {
            OSSystemCPUInfo_t* info = buffer;
            if (bufferSize < sizeof(OSSystemCPUInfo_t)) {
                return OS_EINVALPARAMS;
            }
            info->NumberOfProcessors = atomic_load(&GetMachine()->NumberOfProcessors);
            info->NumberOfActiveCores = atomic_load(&GetMachine()->NumberOfActiveCores);
            *bytesQueriedOut = sizeof(OSSystemCPUInfo_t);
            return OS_EOK;
        } break;
        case OSSYSTEMQUERY_MEMINFO: {
            OSSystemMemoryInfo_t* info = buffer;
            if (bufferSize < sizeof(OSSystemMemoryInfo_t)) {
                return OS_EINVALPARAMS;
            }
            size_t maxBlocks  = GetMachine()->NumberOfMemoryBlocks;
            size_t freeBlocks = GetMachine()->NumberOfFreeMemoryBlocks;
            info->AllocationGranularityBytes = GetMachine()->MemoryGranularity;
            info->PageSizeBytes = GetMemorySpacePageSize();
            info->PagesTotal = maxBlocks;
            info->PagesUsed  = maxBlocks - freeBlocks;
            *bytesQueriedOut = sizeof(OSSystemMemoryInfo_t);
            return OS_EOK;
        } break;
        default: {
            return OS_ENOTSUPPORTED;
        }
    }
}

oserr_t
ScFlushHardwareCache(
    _In_     int    Cache,
    _In_Opt_ void*  Start, 
    _In_Opt_ size_t Length)
{
    if (Cache == CACHE_INSTRUCTION) {
        CpuFlushInstructionCache(Start, Length);
        return OS_EOK;
    }
    else if (Cache == CACHE_MEMORY) {
        CpuInvalidateMemoryCache(Start, Length);
        return OS_EOK;
    }
    return OS_EUNKNOWN;
}

oserr_t
ScInstallSignalHandler(
        _In_ uintptr_t handler)
{
    return MemorySpaceSetSignalHandler(GetCurrentMemorySpace(), handler);
}

oserr_t
ScMapBootFramebuffer(
        _Out_ void** bufferOut)
{
    MemorySpace_t* memorySpace     = GetCurrentMemorySpace();
    uintptr_t      addressPhysical = VideoGetTerminal()->FrameBufferAddressPhysical;
    uintptr_t      fbVirtual       = 0;
    size_t         fbSize          = VideoGetTerminal()->Info.BytesPerScanline * VideoGetTerminal()->Info.Height;
    oserr_t        oserr;

    if (!VideoGetTerminal()->FrameBufferAddressPhysical) {
        return OS_ENOTSUPPORTED;
    }

    oserr = MemorySpaceMap(
            memorySpace,
            &(struct MemorySpaceMapOptions) {
                .PhysicalStart = addressPhysical,
                .Length = fbSize,
                .Flags = MAPPING_COMMIT | MAPPING_USERSPACE | MAPPING_NOCACHE | MAPPING_PERSISTENT,
                .PlacementFlags = MAPPING_PHYSICAL_CONTIGUOUS | MAPPING_VIRTUAL_PROCESS
            },
            &fbVirtual

    );
    if (oserr == OS_EOK) {
        *bufferOut = (void*)fbVirtual;
    }
    return oserr;
}

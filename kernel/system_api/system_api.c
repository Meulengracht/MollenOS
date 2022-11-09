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
#include <os/mollenos.h>
#include <memoryspace.h>
#include <threading.h>
#include <console.h>
#include <machine.h>
#include <debug.h>

oserr_t
ScSystemDebug(
    _In_ int         level,
    _In_ const char* message)
{
    if (message == NULL) {
        return OS_EINVALPARAMS;
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
    return OS_EOK;
}

oserr_t ScEndBootSequence(void) {
    TRACE("Ending console session");
    LogSetRenderMode(0);
    return OS_EOK;
}

oserr_t
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
    return OS_EOK;
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
ScQueryDisplayInformation(
    _In_ VideoDescriptor_t *Descriptor) {
    if (Descriptor == NULL) {
        return OS_EUNKNOWN;
    }
    return VideoQuery(Descriptor);
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

    oserr = MemorySpaceMapContiguous(
            memorySpace,
            &fbVirtual,
            addressPhysical,
            fbSize,
            MAPPING_COMMIT | MAPPING_USERSPACE | MAPPING_NOCACHE | MAPPING_PERSISTENT,
            MAPPING_VIRTUAL_PROCESS
    );

    if (oserr == OS_EOK) {
        *bufferOut = (void*)fbVirtual;
    }
    return oserr;
}

oserr_t
ScMapRamdisk(
        _Out_ void**  bufferOut,
        _Out_ size_t* lengthOut)
{
    oserr_t osStatus;
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
    if (osStatus == OS_EOK) {
        *bufferOut = (void*)mapping;
        *lengthOut = GetMachine()->BootInformation.Ramdisk.Length;
    }
    return osStatus;
}

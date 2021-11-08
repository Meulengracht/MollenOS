/* MollenOS
 *
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
 * along with this program.If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * MollenOS MCore - System Calls
 */
#define __MODULE "SCIF"
//#define __TRACE

#include <modules/manager.h>
#include <arch/output.h>
#include <arch/utils.h>
#include <os/mollenos.h>
#include <memoryspace.h>
#include <threading.h>
#include <console.h>
#include <machine.h>
#include <timers.h>
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
    int MaxBlocks = GetMachine()->PhysicalMemory.capacity;
    int FreeBlocks = GetMachine()->PhysicalMemory.index;
    
    Descriptor->NumberOfProcessors  = atomic_load(&GetMachine()->NumberOfProcessors);
    Descriptor->NumberOfActiveCores = atomic_load(&GetMachine()->NumberOfActiveCores);

    Descriptor->AllocationGranularityBytes = GetMachine()->MemoryGranularity;
    Descriptor->PageSizeBytes              = GetMemorySpacePageSize();
    Descriptor->PagesTotal                 = MaxBlocks;
    Descriptor->PagesUsed                  = MaxBlocks - FreeBlocks;
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
ScSystemTime(
    _In_ SystemTime_t* systemTime)
{
    if (systemTime == NULL) {
        return OsError;
    }
    memcpy(systemTime, &GetMachine()->SystemTime, sizeof(SystemTime_t));
    return OsSuccess;
}

OsStatus_t
ScSystemTick(
    _In_ int              tickBase,
    _In_ LargeUInteger_t* tick)
{
    if (tick == NULL) {
        return OsError;
    }

    if (TimersGetSystemTick((clock_t*)&tick->QuadPart) == OsSuccess) {
        if (tickBase == TIME_PROCESS) {
            SystemModule_t* Module = GetCurrentModule();
            if (Module != NULL) {
                tick->QuadPart -= Module->StartedAt;
            }
        }
        else if (tickBase == TIME_THREAD) {
            Thread_t* Thread = ThreadCurrentForCore(ArchGetProcessorCoreId());
            if (Thread != NULL) {
                tick->QuadPart -= ThreadStartTime(Thread);
            }
        }
        return OsSuccess;
    }

    // Default the result to 0 to indicate unsupported
    tick->QuadPart = 0;
    return OsError;
}

OsStatus_t
ScPerformanceFrequency(
    _Out_ LargeInteger_t *Frequency)
{
    // Sanitize input
    if (Frequency == NULL) {
        return OsError;
    }
    return TimersQueryPerformanceFrequency(Frequency);
}

OsStatus_t
ScPerformanceTick(
    _Out_ LargeInteger_t *Value)
{
    if (Value == NULL) {
        return OsError;
    }
    return TimersQueryPerformanceTick(Value);
}

OsStatus_t
ScQueryDisplayInformation(
    _In_ VideoDescriptor_t *Descriptor) {
    if (Descriptor == NULL) {
        return OsError;
    }
    return VideoQuery(Descriptor);
}

void*
ScCreateDisplayFramebuffer(void)
{
    MemorySpace_t* memorySpace     = GetCurrentMemorySpace();
    uintptr_t      addressPhysical = VideoGetTerminal()->FrameBufferAddressPhysical;
    uintptr_t      fbVirtual       = 0;
    size_t         fbSize          = VideoGetTerminal()->Info.BytesPerScanline * VideoGetTerminal()->Info.Height;

    if (MemorySpaceMapContiguous(memorySpace, &fbVirtual, addressPhysical, fbSize,
        MAPPING_COMMIT | MAPPING_USERSPACE | MAPPING_NOCACHE | MAPPING_PERSISTENT,
                                 MAPPING_VIRTUAL_PROCESS) != OsSuccess) {
        // What? @todo
        ERROR("Failed to map the display buffer");
    }
    return (void*)fbVirtual;
}

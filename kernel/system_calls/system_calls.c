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

OsStatus_t
ScSystemDebug(
    _In_ int         Type,
    _In_ const char* Module,
    _In_ const char* Message)
{
    if (Module == NULL || Message == NULL) {
        return OsError;
    }

    // Switch based on type
    if (Type == 0) {
        LogAppendMessage(LogTrace, Module, Message);
    }
    else if (Type == 1) {
        LogAppendMessage(LogDebug, Module, Message);
    }
    else {
        LogAppendMessage(LogError, Module, Message);
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
    Descriptor->NumberOfProcessors  = GetMachine()->NumberOfProcessors;
    Descriptor->NumberOfActiveCores = GetMachine()->NumberOfActiveCores;

    Descriptor->AllocationGranularityBytes = GetMachine()->MemoryGranularity;
    Descriptor->PageSizeBytes              = GetMemorySpacePageSize();
    Descriptor->PagesTotal                 = GetMachine()->PhysicalMemory.BlockCount;
    Descriptor->PagesUsed                  = GetMachine()->PhysicalMemory.BlocksAllocated;
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
    _In_ SystemTime_t* SystemTime)
{
    if (SystemTime == NULL) {
        return OsError;
    }
    memcpy(SystemTime, &GetMachine()->SystemTime, sizeof(SystemTime_t));
    return OsSuccess;
}

OsStatus_t
ScSystemTick(
    _In_ int              TickBase,
    _In_ LargeUInteger_t* Tick)
{
    if (Tick == NULL) {
        return OsError;
    }

    if (TimersGetSystemTick((clock_t*)&Tick->QuadPart) == OsSuccess) {
        if (TickBase == TIME_PROCESS) {
            SystemModule_t* Module = GetCurrentModule();
            if (Module != NULL) {
                Tick->QuadPart -= Module->StartedAt;
            }
        }
        else if (TickBase == TIME_THREAD) {
            MCoreThread_t* Thread = GetCurrentThreadForCore(ArchGetProcessorCoreId());
            if (Thread != NULL) {
                Tick->QuadPart -= Thread->StartedAt;
            }
        }
        return OsSuccess;
    }

    // Default the result to 0 to indicate unsupported
    Tick->QuadPart = 0;
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
    SystemModule_t*      Module     = GetCurrentModule();
    SystemMemorySpace_t* Space      = GetCurrentMemorySpace();
    uintptr_t            FbPhysical = VideoGetTerminal()->FrameBufferAddressPhysical;
    uintptr_t            FbVirtual  = 0;
    size_t               FbSize     = VideoGetTerminal()->Info.BytesPerScanline * VideoGetTerminal()->Info.Height;
    
    if (Module == NULL) {
        return NULL;
    }

    if (CreateMemorySpaceMapping(Space, &FbPhysical, &FbVirtual, FbSize, 
        MAPPING_COMMIT | MAPPING_USERSPACE | MAPPING_NOCACHE | MAPPING_PERSISTENT,
        MAPPING_VIRTUAL_PROCESS | MAPPING_PHYSICAL_FIXED, __MASK) != OsSuccess) {
        // What? @todo
        ERROR("Failed to map the display buffer");
    }
    return (void*)FbVirtual;
}

OsStatus_t
ScIsServiceAvailable(
    _In_ UUId_t ServiceId)
{
    if (GetModuleByHandle(ServiceId) != NULL) {
        return OsSuccess;
    }
    return OsDoesNotExist;
}

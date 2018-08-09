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

#include <os/osdefs.h>
#include <os/mollenos.h>
#include <os/contracts/video.h>
#include <process/phoenix.h>
#include <system/video.h>
#include <system/utils.h>
#include <memoryspace.h>
#include <threading.h>
#include <timers.h>
#include <video.h>
#include <debug.h>

/* ScSystemDebug 
 * Debug/trace printing for userspace application and drivers */
OsStatus_t
ScSystemDebug(
    _In_ int            Type,
    _In_ const char*    Module,
    _In_ const char*    Message)
{
    // Validate params
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

/* ScFlushHardwareCache
 * Flushes the specified hardware cache. Should be used with caution as it might
 * result in performance drops. */
OsStatus_t ScFlushHardwareCache(
    _In_     int    Cache,
    _In_Opt_ void*  Start, 
    _In_Opt_ size_t Length) {
    if (Cache == CACHE_INSTRUCTION) {
        CpuFlushInstructionCache(Start, Length);
        return OsSuccess;
    }
    return OsError;
}

/* System (Environment) Query 
 * This function allows the user to query 
 * information about cpu, memory, stats etc */
int ScEnvironmentQuery(void) {
    return 0;
}

/* ScSystemTime
 * Retrieves the system time. This is only ticking
 * if a system clock has been initialized. */
OsStatus_t
ScSystemTime(
    _Out_ struct tm *SystemTime)
{
    // Sanitize input
    if (SystemTime == NULL) {
        return OsError;
    }
    return TimersGetSystemTime(SystemTime);
}

/* ScSystemTick
 * Retrieves the system tick counter. This is only ticking
 * if a system timer has been initialized. */
OsStatus_t
ScSystemTick(
    _In_  int       TickBase,
    _Out_ clock_t*  SystemTick)
{
    if (SystemTick == NULL) {
        return OsError;
    }

    switch (TickBase) {
        case TIME_MONOTONIC: {
            return TimersGetSystemTick(SystemTick);
        } break;
        case TIME_PROCESS: {
            MCoreAsh_t* Process = PhoenixGetCurrentAsh();
            if (Process != NULL) {
                *SystemTick = Process->StartedAt;
            }
        } break;
        case TIME_THREAD: {
            MCoreThread_t* Thread = ThreadingGetCurrentThread(CpuGetCurrentId());
            if (Thread != NULL) {
                *SystemTick = Thread->StartedAt;
            }
        } break;

        default: {
            *SystemTick = 0;
        } break;
    }
    return OsSuccess;
}

/* ScPerformanceFrequency
 * Returns how often the performance timer fires every
 * second, the value will never be 0 */
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

/* ScPerformanceTick
 * Retrieves the system performance tick counter. This is only ticking
 * if a system performance timer has been initialized. */
OsStatus_t
ScPerformanceTick(
    _Out_ LargeInteger_t *Value)
{
    // Sanitize input
    if (Value == NULL) {
        return OsError;
    }
    return TimersQueryPerformanceTick(Value);
}

/* ScQueryDisplayInformation
 * Queries information about the active display */
OsStatus_t
ScQueryDisplayInformation(
    _In_ VideoDescriptor_t *Descriptor) {
    if (Descriptor == NULL) {
        return OsError;
    }
    return VideoQuery(Descriptor);
}

/* ScCreateDisplayFramebuffer
 * Right now it simply identity maps the screen display framebuffer
 * into the current process's memory mappings and returns a pointer to it. */
void*
ScCreateDisplayFramebuffer(void) {
    uintptr_t FbPhysical    = VideoGetTerminal()->FrameBufferAddressPhysical;
    uintptr_t FbVirtual     = 0;
    size_t FbSize           = VideoGetTerminal()->Info.BytesPerScanline * VideoGetTerminal()->Info.Height;

    // Sanitize
    if (PhoenixGetCurrentAsh() == NULL) {
        return NULL;
    }

    // Allocate the neccessary size
    FbVirtual = AllocateBlocksInBlockmap(PhoenixGetCurrentAsh()->Heap, __MASK, FbSize);
    if (FbVirtual == 0) {
        return NULL;
    }

    if (CreateSystemMemorySpaceMapping(GetCurrentSystemMemorySpace(), &FbPhysical, &FbVirtual,
        FbSize, MAPPING_USERSPACE | MAPPING_NOCACHE | MAPPING_FIXED | MAPPING_PROVIDED | MAPPING_PERSISTENT, __MASK) != OsSuccess) {
        // What? @todo
        ERROR("Failed to map the display buffer");
    }
    return (void*)FbVirtual;
}

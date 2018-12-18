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
#include <system/output.h>
#include <system/utils.h>
#include <os/mollenos.h>
#include <memoryspace.h>
#include <threading.h>
#include <timers.h>
#include <console.h>
#include <debug.h>

/* ScSystemDebug 
 * Debug/trace printing for userspace application and drivers */
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

/* ScFlushHardwareCache
 * Flushes the specified hardware cache. Should be used with caution as it might
 * result in performance drops. */
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
    _Out_ struct tm* SystemTime)
{
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

    if (TimersGetSystemTick(SystemTick) == OsSuccess) {
        if (TickBase == TIME_PROCESS) {
            SystemModule_t* Module = GetCurrentModule();
            if (Module != NULL) {
                *SystemTick -= Module->StartedAt;
            }
        }
        else if (TickBase == TIME_THREAD) {
            MCoreThread_t* Thread = GetCurrentThreadForCore(CpuGetCurrentId());
            if (Thread != NULL) {
                *SystemTick -= Thread->StartedAt;
            }
        }
        return OsSuccess;
    }

    // Default the result to 0 to indicate unsupported
    *SystemTick = 0;
    return OsError;
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
ScCreateDisplayFramebuffer(void)
{
    SystemMemorySpace_t* Space      = GetCurrentMemorySpace();
    uintptr_t            FbPhysical = VideoGetTerminal()->FrameBufferAddressPhysical;
    uintptr_t            FbVirtual  = 0;
    size_t               FbSize     = VideoGetTerminal()->Info.BytesPerScanline * VideoGetTerminal()->Info.Height;
    // @todo security
    assert(Space->Context != NULL);

    // Allocate the neccessary size
    FbVirtual = AllocateBlocksInBlockmap(Space->Context->HeapSpace, __MASK, FbSize);
    if (FbVirtual == 0) {
        return NULL;
    }

    if (CreateMemorySpaceMapping(Space, &FbPhysical, &FbVirtual,
        FbSize, MAPPING_USERSPACE | MAPPING_NOCACHE | MAPPING_FIXED | MAPPING_PROVIDED | MAPPING_PERSISTENT, __MASK) != OsSuccess) {
        // What? @todo
        ERROR("Failed to map the display buffer");
    }
    return (void*)FbVirtual;
}

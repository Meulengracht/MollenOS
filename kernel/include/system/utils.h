/* MollenOS
 *
 * Copyright 2011 - 2017, Philip Meulengracht
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
 * MollenOS Utility Interface
 * - Contains the shared kernel utility interface
 *   that all sub-layers / architectures must conform to
 */

#ifndef _MCORE_UTILS_H_
#define _MCORE_UTILS_H_

#include <os/osdefs.h>
#include <os/spinlock.h>
#include <os/context.h>
#include <time.h>

/* ContextDump 
 * Dumps the contents of the given context for debugging */
KERNELAPI OsStatus_t KERNELABI
ContextDump(
    _In_ Context_t *Context);

/* CpuGetCurrentId 
 * Retrieves the current cpu id for caller */
KERNELAPI UUId_t KERNELABI
CpuGetCurrentId(void);

/* CpuIdle
 * Enters idle mode for the current cpu */
KERNELAPI void KERNELABI
CpuIdle(void);

/* CpuHalt
 * Halts the current cpu - rendering system useless */
KERNELAPI void KERNELABI
CpuHalt(void);

/* CpuGetTicks
 * Get the ticks for the current cpu. */
KERNELAPI size_t KERNELABI
CpuGetTicks(void);

/* CpuGetTime
 * Retrieves the time for the system. */
KERNELAPI OsStatus_t KERNELABI
CpuGetTime(
    _Out_ struct tm *SystemTime);

/* CpuStall
 * Stalls the cpu for the given milliseconds, blocking call. */
KERNELAPI void KERNELABI
CpuStall(
    size_t MilliSeconds);

/* CpuFlushInstructionCache
 * Flushes the instruction cache for the processor. */
KERNELAPI void KERNELABI
CpuFlushInstructionCache(
    _In_Opt_ void*  Start, 
    _In_Opt_ size_t Length);

#endif //!_MCORE_UTILS_H_

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
 * Utility Interface
 * - Contains the shared kernel utility interface
 *   that all sub-layers / architectures must conform to
 */

#ifndef __SYSTEM_INTERFACE_UTILS_H__
#define __SYSTEM_INTERFACE_UTILS_H__

#include <os/osdefs.h>
#include <os/context.h>

/* ArchDumpThreadContext 
 * Dumps the contents of the given thread context for debugging. */
KERNELAPI OsStatus_t KERNELABI
ArchDumpThreadContext(
    _In_ Context_t *Context);

/* ArchGetProcessorCoreId 
 * Returns the current processor core id. */
KERNELAPI UUId_t KERNELABI
ArchGetProcessorCoreId(void);

/* ArchProcessorIdle
 * Enters idle mode for the current processor core. */
KERNELAPI void KERNELABI
ArchProcessorIdle(void);

/* ArchProcessorHalt
 * Halts the current cpu - rendering system useless. */
KERNELAPI void KERNELABI
ArchProcessorHalt(void);

/* CpuFlushInstructionCache
 * Flushes the instruction cache for the processor. */
KERNELAPI void KERNELABI
CpuFlushInstructionCache(
    _In_Opt_ void*  Start, 
    _In_Opt_ size_t Length);

/* CpuInvalidateMemoryCache
 * Invalidates a memory area in the memory cache. */
KERNELAPI void KERNELABI
CpuInvalidateMemoryCache(
    _In_Opt_ void*  Start, 
    _In_Opt_ size_t Length);

#endif //!__SYSTEM_INTERFACE_UTILS_H__

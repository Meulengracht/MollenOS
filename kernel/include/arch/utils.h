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
 *
 * Utility Interface
 * - Contains the shared kernel utility interface
 *   that all sub-layers / architectures must conform to
 */

#ifndef __SYSTEM_INTERFACE_UTILS_H__
#define __SYSTEM_INTERFACE_UTILS_H__

#include <os/osdefs.h>

DECL_STRUCT(Context);
DECL_STRUCT(SystemCpu);

/**
 * @brief Converts a dma type into a page mask used for physical page allocation. This call
 * is specific to the dma interface and provides a way for platforms to identify which pages
 * can be used for each dma type.
 *
 * @param dmaType     [In]
 * @param pageMaskOut [Out]
 * @return
 */
KERNELAPI OsStatus_t KERNELABI
ArchGetPageMaskFromDmaType(
        _In_  unsigned int dmaType,
        _Out_ size_t*      pageMaskOut);

/**
 * @brief Dumps the contents of the given thread context for debugging.
 *
 * @param context
 * @return
 */
KERNELAPI OsStatus_t KERNELABI
ArchDumpThreadContext(
    _In_ Context_t *context);

/**
 * @brief Returns the current processor core id.
 *
 * @return
 */
KERNELAPI UUId_t KERNELABI
ArchGetProcessorCoreId(void);

/**
 * @brief Initializes and fills in the processor structure for the calling processor.
 *
 * @param Processor
 */
KERNELAPI void KERNELABI
ArchProcessorInitialize(
    _In_ SystemCpu_t* Processor);

/**
 * @brief Sends the given interrupt vector to the core specified.
 *
 * @param CoreId
 * @param InterruptId
 * @return
 */
KERNELAPI OsStatus_t KERNELABI
ArchProcessorSendInterrupt(
    _In_ UUId_t CoreId,
    _In_ UUId_t InterruptId);

/**
 * @brief Enters idle mode for the current processor core.
 */
KERNELAPI void KERNELABI
ArchProcessorIdle(void);

/**
 * @brief Halts the current cpu - rendering system useless.
 */
KERNELAPI void KERNELABI
ArchProcessorHalt(void);

/**
 * @brief Flushes the instruction cache for the processor.
 *
 * @param Start
 * @param Length
 */
KERNELAPI void KERNELABI
CpuFlushInstructionCache(
    _In_Opt_ void*  Start, 
    _In_Opt_ size_t Length);

/**
 * @brief Invalidates a memory area in the memory cache.
 *
 * @param Start
 * @param Length
 */
KERNELAPI void KERNELABI
CpuInvalidateMemoryCache(
    _In_Opt_ void*  Start, 
    _In_Opt_ size_t Length);

#endif //!__SYSTEM_INTERFACE_UTILS_H__

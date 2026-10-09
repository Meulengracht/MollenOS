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
#include <os/types/memory.h>

DECL_STRUCT(Context);
DECL_STRUCT(DeviceTree);
DECL_STRUCT(SystemCpu);
DECL_STRUCT(SystemCpuCore);

/**
 * @brief Converts a dma type into a page mask used for physical page allocation. This call
 * is specific to the dma interface and provides a way for platforms to identify which pages
 * can be used for each dma type.
 *
 * @param conformity [In]
 * @param pageMaskOut [Out]
 * @return
 */
KERNELAPI oserr_t KERNELABI
ArchSHMTypeToPageMask(
        _In_  enum OSMemoryConformity conformity,
        _Out_ size_t*                 pageMaskOut);

/**
 * @brief Returns the current processor core id.
 *
 * @return
 */
KERNELAPI uuid_t KERNELABI
ArchGetProcessorCoreId(void);

/**
 * @brief Initializes the underlying platform and fills the cpu and core structure,
 * with any available data.
 *
 * @param[In] cpu  A pointer to primary CPU structure for the machine.
 * @param[In] core A pointer to a structure describing the boot-core.
 */
KERNELAPI void KERNELABI
ArchPlatformInitialize(
        _In_ SystemCpu_t*     cpu,
        _In_ SystemCpuCore_t* core);

/**
 * @brief Sends the given interrupt vector to the core specified.
 *
 * @param coreId
 * @param interruptId
 * @return
 */
KERNELAPI oserr_t KERNELABI
ArchProcessorSendInterrupt(
        _In_ uuid_t coreId,
        _In_ uuid_t interruptId);

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

/**
 * @brief Smallest data cache line size in bytes.
 *
 * Cache maintenance always works on whole lines, so callers use this to tell
 * whether a block of memory shares a line with unrelated data.
 */
KERNELAPI size_t KERNELABI
CpuDataCacheLineSize(void);

/**
 * @brief Write cached CPU data for a block of physical RAM back to memory.
 *
 * A device that cannot see the CPU cache reads memory directly, so data the
 * CPU wrote must reach memory before such a device reads it. The cached copy
 * stays valid. Whole cache lines touching the block are affected. Returns once
 * the work is complete. Does nothing where devices see the CPU cache.
 */
KERNELAPI void KERNELABI
CpuDataCacheClean(
    _In_ uintptr_t physical,
    _In_ size_t    length);

/**
 * @brief Throw away cached copies of a block of physical RAM.
 *
 * After a device wrote memory directly, older cached copies would hide the new
 * data from the CPU. Any unwritten CPU changes in the affected lines are lost,
 * so callers must own every byte of every line touching the block. Returns once
 * the work is complete. Does nothing where devices see the CPU cache.
 */
KERNELAPI void KERNELABI
CpuDataCacheInvalidate(
    _In_ uintptr_t physical,
    _In_ size_t    length);

/**
 * @brief Write back, then throw away, cached copies of a block of physical RAM.
 *
 * Used before a device writes memory: writing back first means no CPU data is
 * lost, and throwing away the lines means no old cached line can later be
 * written over the device's data. Returns once the work is complete. Does
 * nothing where devices see the CPU cache.
 */
KERNELAPI void KERNELABI
CpuDataCacheCleanInvalidate(
    _In_ uintptr_t physical,
    _In_ size_t    length);

/**
 * @brief Discover and register the platform components from a validated DTB. 
 * @param tree A pointer to the validated device tree blob (DTB) containing platform components.
 * @return Returns OS_EOK on success, or an appropriate error code on failure.
 */
KERNELAPI oserr_t KERNELABI
ArchDeviceTreeInitialize(
    _In_ const DeviceTree_t* tree);

#endif //!__SYSTEM_INTERFACE_UTILS_H__

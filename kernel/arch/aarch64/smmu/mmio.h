/**
 * Copyright, Philip Meulengracht
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
 * SMMU Register Access (MMIO)
 * Helpers for reading and writing SMMU controller registers through the kernel
 * virtual address returned by Arm64MapDevice.
 *
 * Callers must pass an offset inside that mapped window, aligned to 4 bytes
 * for 32-bit accesses or 8 bytes for 64-bit accesses. Host unit tests swap
 * these out per backend to route reads and writes into fake register models.
 */

#ifndef __ARM_SMMU_MMIO_H__
#define __ARM_SMMU_MMIO_H__

#include <os/osdefs.h>

/**
 * @brief Reads a 32-bit SMMU hardware register without compiler caching.
 *
 * @param base   Kernel virtual base address of the unit's register window.
 * @param offset 4-byte-aligned byte offset from base.
 * @return The current 32-bit register value.
 */
static inline uint32_t
__ArmSMMURead32(
    _In_ uintptr_t    base,
    _In_ unsigned int offset)
{
    return *(volatile uint32_t*)(base + offset);
}

/**
 * @brief Reads a 64-bit SMMU hardware register without compiler caching.
 *
 * @param base   Kernel virtual base address of the unit's register window.
 * @param offset 8-byte-aligned byte offset from base.
 * @return The current 64-bit register value.
 */
static inline uint64_t
__ArmSMMURead64(
    _In_ uintptr_t    base,
    _In_ unsigned int offset)
{
    return *(volatile uint64_t*)(base + offset);
}

/**
 * @brief Writes a 32-bit SMMU hardware register immediately.
 *
 * @param base   Kernel virtual base address of the unit's register window.
 * @param offset 4-byte-aligned byte offset from base.
 * @param value  32-bit value to write.
 */
static inline void
__ArmSMMUWrite32(
    _In_ uintptr_t    base,
    _In_ unsigned int offset,
    _In_ uint32_t     value)
{
    *(volatile uint32_t*)(base + offset) = value;
}

/**
 * @brief Writes a 64-bit SMMU hardware register immediately.
 *
 * @param base   Kernel virtual base address of the unit's register window.
 * @param offset 8-byte-aligned byte offset from base.
 * @param value  64-bit value to write.
 */
static inline void
__ArmSMMUWrite64(
    _In_ uintptr_t    base,
    _In_ unsigned int offset,
    _In_ uint64_t     value)
{
    *(volatile uint64_t*)(base + offset) = value;
}

/**
 * @brief Ensures earlier memory and register operations finish before later ones.
 *
 * A full-system Data Synchronization Barrier (dsb sy) is needed in four spots:
 * - Before updating a register or queue producer index, so table entries and
 *   command descriptors reach RAM before the SMMU tries to read them.
 * - Between writing a control register and reading back a status register, so
 *   the controller sees the write before we poll for completion.
 * - After reading a hardware-updated queue index, so the CPU reads newly
 *   written event entries rather than stale data from before the index moved.
 * - Full-system scope (sy) covers control memory pools mapped as non-cacheable
 *   Normal memory as well as coherent device memory.
 */
static inline void
__ArmSMMUBarrier(void)
{
    __asm__ volatile("dsb sy" ::: "memory");
}

#endif //!__ARM_SMMU_MMIO_H__

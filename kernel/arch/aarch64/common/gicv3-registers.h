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
 */

#ifndef __VALI_ARM64_GICV3_REGISTERS_H__
#define __VALI_ARM64_GICV3_REGISTERS_H__

/**
 * @brief Reads a 32-bit register from a mapped GIC frame. The register must
 * be naturally aligned.
 *
 * @param base   The virtual address of the mapped register frame.
 * @param offset The byte offset of the register in the frame.
 * @return The register value. No barrier is issued, so the caller must take
 *         care of ordering.
 */
static inline uint32_t
__GicRead32(
    _In_ uintptr_t base,
    _In_ unsigned int offset)
{
    return *(volatile uint32_t*)(base + offset);
}

/**
 * @brief Writes a 32-bit register in a mapped GIC frame. The register must
 * be naturally aligned.
 *
 * @param base   The virtual address of the mapped register frame.
 * @param offset The byte offset of the register in the frame.
 * @param value  The value to write. For set/clear registers these are the
 *               command bits.
 */
static inline void
__GicWrite32(
    _In_ uintptr_t base,
    _In_ unsigned int offset,
    _In_ uint32_t value)
{
    *(volatile uint32_t*)(base + offset) = value;
}

/**
 * @brief Reads a 64-bit register from a mapped GIC frame. The register must
 * be naturally aligned.
 *
 * @param base   The virtual address of the mapped register frame.
 * @param offset The byte offset of the register in the frame.
 * @return The register value. No barrier is issued, so the caller must take
 *         care of ordering.
 */
static inline uint64_t
__GicRead64(
    _In_ uintptr_t base,
    _In_ unsigned int offset)
{
    return *(volatile uint64_t*)(base + offset);
}

/**
 * @brief Writes a 64-bit register in a mapped GIC frame. The register must
 * be naturally aligned.
 *
 * @param base   The virtual address of the mapped register frame.
 * @param offset The byte offset of the register in the frame.
 * @param value  The value to write. For set/clear registers these are the
 *               command bits.
 */
static inline void
__GicWrite64(
    _In_ uintptr_t base,
    _In_ unsigned int offset,
    _In_ uint64_t value)
{
    *(volatile uint64_t*)(base + offset) = value;
}

#define GIC_READ_SYSTEM(name, value)  __asm__ volatile("mrs %0, " #name : "=r"(value))
#define GIC_WRITE_SYSTEM(name, value) __asm__ volatile("msr " #name ", %0" :: "r"((uint64_t)(value)) : "memory")
#define GIC_BARRIER()                 __asm__ volatile("dsb sy\nisb" ::: "memory")
#define GIC_RELAX()                   __asm__ volatile("yield" ::: "memory")

#endif //!__VALI_ARM64_GICV3_REGISTERS_H__

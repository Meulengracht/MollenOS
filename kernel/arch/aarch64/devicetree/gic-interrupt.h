/**
 * Copyright 2026, Philip Meulengracht
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
 * ARM GIC Device Tree Interrupts
 * Converts the three-cell interrupt specifier of a GIC node into the interrupt
 * ID the kernel and the GIC hardware use.
 */

#ifndef __ARM64_DT_GIC_INTERRUPT_H__
#define __ARM64_DT_GIC_INTERRUPT_H__

#include <devicetree.h>

/**
 * @brief One interrupt decoded from a GIC interrupt specifier.
 *
 * Line is the GIC interrupt ID, which is also the kernel's interrupt number.
 * Private is true for a per-core interrupt (PPI) and false for a shared one
 * (SPI). Flags is the raw third cell. Its low nibble holds the trigger type,
 * and callers decide which triggers they accept.
 */
typedef struct Arm64GicInterrupt {
    uint32_t Line;
    uint32_t Flags;
    bool     Private;
} Arm64GicInterrupt_t;

/**
 * @brief Decode the specifier cells <type number flags> of a GIC interrupt.
 *
 * The tree counts SPIs and PPIs from zero. This adds the ID where each type
 * starts in the GIC, so SPI 5 becomes interrupt 37. The caller must already
 * know that reference->Node is the GIC it expects.
 *
 * @param reference Interrupt read with DeviceTreeReadInterrupt.
 * @param interruptOut Receives the decoded interrupt on success.
 * @return OS_EOK on success, OS_ENOTSUPPORTED if the specifier does not have
 *         three cells or its type is not SPI or PPI, or OS_EINVALPARAMS if the
 *         number is outside the range of its type.
 */
KERNELAPI oserr_t KERNELABI
Arm64GicDecodeInterrupt(
    _In_  const DeviceTreeReference_t* reference,
    _Out_ Arm64GicInterrupt_t*         interruptOut);

#endif //!__ARM64_DT_GIC_INTERRUPT_H__

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
 * ARM SMMU Hardware Description
 * Plain description structs filled in by ACPI or Device Tree discovery.
 */

#ifndef __ARM_SMMU_DESCRIPTION_H__
#define __ARM_SMMU_DESCRIPTION_H__

#include <os/osdefs.h>

// Supported ARM System Memory Management Unit (SMMU) hardware families.
// An SMMU sits between DMA-capable devices and RAM to translate device
// addresses into real physical addresses, much like the CPU MMU does for
// software. SMMUv3 is the newer architecture, while MMU-500 is an older
// SMMUv2-class controller.
enum ArmSMMUKind {
    ArmSMMUv3,
    ArmSMMU500
};

/**
 * @brief One wired interrupt line copied from firmware tables.
 *
 * Line holds the kernel interrupt number, or UINT32_MAX when firmware did not
 * wire this interrupt. Flags uses the kernel's INTERRUPT_ACPICONFORM_* bits to
 * record trigger mode and polarity, and stays zero when the line is absent.
 * Discovery only records these values and leaves interrupt registration and
 * MSI setup to the SMMU driver.
 */
typedef struct ArmSMMUInterrupt {
    uint32_t Line;
    uint32_t Flags;
} ArmSMMUInterrupt_t;

/**
 * @brief Firmware description of one supported SMMU controller.
 *
 * RegisterBase and RegisterLength give the physical MMIO address range (in
 * bytes) for the unit's control registers. TableCoherent is true when the SMMU
 * reads its translation page tables through the CPU caches. When false, the
 * driver must flush cache lines to RAM after writing table entries. This flag
 * applies to the SMMU's own table walks and can differ from the cache policy
 * of a device behind it.
 *
 * For SMMUv3, EventQueue reports translation faults and hardware events, and
 * GlobalError reports fatal unit errors. For MMU-500, GlobalFault and
 * ConfigurationFault signal unit-wide errors, while ContextInterrupts holds
 * one fault line per hardware translation slot (context bank) up to
 * ContextInterruptCount. If firmware shares one interrupt wire across several
 * context banks, the same ArmSMMUInterrupt_t is repeated in each slot.
 *
 * Discovery copies these fields without mapping registers. The driver still
 * needs to probe hardware capabilities and check for state left active by
 * firmware.
 */
typedef struct ArmSMMUDescription {
    enum ArmSMMUKind Kind;
    uint64_t         RegisterBase;
    uint64_t         RegisterLength;
    bool             TableCoherent;
    union {
        struct {
            ArmSMMUInterrupt_t EventQueue;
            ArmSMMUInterrupt_t GlobalError;
        } V3;
        struct {
            ArmSMMUInterrupt_t GlobalFault;
            ArmSMMUInterrupt_t ConfigurationFault;
            uint32_t           ContextInterruptCount;
            ArmSMMUInterrupt_t ContextInterrupts[128];
        } Mmu500;
    };
} ArmSMMUDescription_t;

#endif //!__ARM_SMMU_DESCRIPTION_H__

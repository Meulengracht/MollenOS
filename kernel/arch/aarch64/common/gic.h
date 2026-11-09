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

#ifndef __VALI_ARM64_GIC_H__
#define __VALI_ARM64_GIC_H__

#include <arch/aarch64/arch.h>
#include <interrupts/interrupts.h>

/**
 * @brief GIC operations selected once, before enabling interrupts or starting
 * secondary CPUs.
 *
 * Configure requires masked local IRQs and the configuration lock.
 * Acknowledge returns a hardware token. Decode extracts its interrupt line.
 * Complete must receive the original token, including the sending CPU for
 * GICv2 SGIs. Private registers always refer to the calling CPU.
 */
struct Arm64GicOperations {
    oserr_t  (*Initialize)(unsigned int* count);
    oserr_t  (*InitializeCore)(void);
    oserr_t  (*Configure)(const InterruptControllerResource_t* interrupt, int enable);
    void     (*EnablePrivate)(unsigned int line);
    uint32_t (*Acknowledge)(void);
    uint32_t (*Decode)(uint32_t token);
    void     (*Complete)(uint32_t token);
    oserr_t  (*Send)(PlatformCpuCoreBlock_t* core, unsigned int line);
};

extern const struct Arm64GicOperations g_arm64GicV2;
extern const struct Arm64GicOperations g_arm64GicV3;

#endif //!__VALI_ARM64_GIC_H__

/**
 * Copyright 2026, Philip Meulengracht
 *
 * This program is free software : you can redistribute it and / or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef __DDK_TYPES_INTERRUPT_CONTROLLER_H__
#define __DDK_TYPES_INTERRUPT_CONTROLLER_H__

#include <ddk/types/interrupt.h>

typedef enum InterruptMessageControllerKind {
    // The platform's shared PCI message fabric, currently provided on x86.
    InterruptMessageControllerKindPlatform = 1,
    // A PCI host node in the boot Device Tree; never a deviced-generated ID.
    InterruptMessageControllerKindDeviceTree
} InterruptMessageControllerKind_t;

/**
 * Read-only message-controller lookup key, not an allocation request or source
 * selector. Resolution returns a boot-lifetime kernel controller ID used for
 * later message allocation; it neither reserves vectors nor authorizes device
 * access. The DT namespace uses a host node's structure offset, not its phandle.
 */
typedef struct InterruptMessageControllerIdentity {
    // Query version; must be one.
    uint32_t                         Version;
    InterruptMessageControllerKind_t Kind;
    // PCI host structure offset in the boot FDT; zero for Platform.
    uint32_t                         FirmwareNode;
    // Must be zero. Source numbers never belong to a message identity.
    uint32_t                         Reserved;
} InterruptMessageControllerIdentity_t;

/**
 * Read-only lookup of a fixed DT source against the active line controller.
 * FirmwareController is a nonzero GIC phandle; Line is the decoded SPI INTID,
 * including its 32-ID offset, rather than the raw DT interrupt cell. Resolution
 * returns a kernel controller ID without reserving or enabling the line.
 */
typedef struct InterruptLineSourceIdentity {
    // Must be one.
    uint32_t Version;
    // DT GIC phandle, translated at resolution.
    uint32_t FirmwareController;
    // Requested SPI INTID, 32 through 1019.
    uint32_t Line;
    // Must be zero.
    uint32_t Reserved;
} InterruptLineSourceIdentity_t;

#endif //!__DDK_TYPES_INTERRUPT_CONTROLLER_H__

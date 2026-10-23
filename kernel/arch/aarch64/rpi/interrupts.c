/**
 * MollenOS
 *
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

#include <arch/aarch64/arch.h>
#include <component/ic.h>
#include <devicetree.h>
#include <interrupts.h>

#include "../common/platform.h"
#include "bcm2711-msi.h"
#include "mip.h"

oserr_t
Arm64PlatformInterruptInitialize(
    _In_ uint32_t interruptCount)
{
    const DeviceTree_t*          tree = DeviceTreeGet();
    SystemInterruptController_t* controller;
    oserr_t                      status;

    if (!tree) {
        return OS_ENOENT;
    }

    controller = GetInterruptControllerByLine(INTERRUPT_PHYSICAL_BASE);
    if (!controller) {
        return OS_ENOENT;
    }

    status = Arm64MipInitialize(tree, controller->Id, interruptCount);
    if (status != OS_EOK) {
        return status;
    }
    return Arm64Bcm2711MsiInitialize(tree, controller->Id, interruptCount);
}

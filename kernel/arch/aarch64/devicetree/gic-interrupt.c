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
 */

#include "../common/private.h"
#include "gic-interrupt.h"

oserr_t
Arm64GicDecodeInterrupt(
    _In_  const DeviceTreeReference_t* reference,
    _Out_ Arm64GicInterrupt_t*         interruptOut)
{
    uint32_t number;

    if (reference->CellCount != ARM64_DT_GIC_CELLS) {
        return OS_ENOTSUPPORTED;
    }

    number = reference->Cells[1];
    switch (reference->Cells[0]) {
        case ARM64_DT_GIC_SPI:
            if (number >= ARM64_GIC_SPI_COUNT) {
                return OS_EINVALPARAMS;
            }
            interruptOut->Line = number + ARM64_GIC_SPI_BASE;
            interruptOut->Private = false;
            break;
        case ARM64_DT_GIC_PPI:
            if (number >= ARM64_GIC_PPI_COUNT) {
                return OS_EINVALPARAMS;
            }
            interruptOut->Line = number + ARM64_GIC_PPI_BASE;
            interruptOut->Private = true;
            break;
        default:
            return OS_ENOTSUPPORTED;
    }

    interruptOut->Flags = reference->Cells[2];
    return OS_EOK;
}

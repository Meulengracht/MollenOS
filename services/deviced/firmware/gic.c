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
 */

#include "resources.h"
#include "interrupt.h"
#include <ddk/interrupt.h>

oserr_t
FdtGicInterrupt(
        _In_ const struct FdtResources* provider,
        _In_ const uint8_t* cells,
        _Out_ struct FdtInterrupt* interrupt)
{
    uint32_t type;
    uint32_t number;
    uint32_t trigger;

    if (provider->InterruptCells != 3) {
        return OS_ENOTSUPPORTED;
    }
    type = FdtReadBe32(cells);
    number = FdtReadBe32(cells + 4);
    trigger = FdtReadBe32(cells + 8);

    if (!(FdtCompatible(&provider->View, "arm,gic-400") ||
        FdtCompatible(&provider->View, "arm,cortex-a15-gic") ||
        FdtCompatible(&provider->View, "arm,gic-v3")) || !provider->IsInterruptController ||
        type != 0 || number > 987 ||
        (trigger != 1 && trigger != 2 && trigger != 4 && trigger != 8)) {
        return OS_ENOTSUPPORTED;
    }
    interrupt->Controller = provider->Phandle;
    interrupt->Line = (int)number + 32;
    interrupt->Flags = INTERRUPT_ACPICONFORM_PRESENT | INTERRUPT_ACPICONFORM_FIXED |
            INTERRUPT_ACPICONFORM_SHAREABLE;
    if (trigger == 4 || trigger == 8) {
        interrupt->Flags |= INTERRUPT_ACPICONFORM_TRIGGERMODE;
    }
    if (trigger == 2 || trigger == 8) {
        interrupt->Flags |= INTERRUPT_ACPICONFORM_POLARITY;
    }
    return OS_EOK;
}

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

#ifndef __FIRMWARE_INTERRUPT_H__
#define __FIRMWARE_INTERRUPT_H__

#include <os/osdefs.h>

/** An interrupt handled by an Arm Generic Interrupt Controller (GIC).
 * Controller is the controller's firmware ID (phandle), Line is the GIC
 * interrupt number, and Flags describes how the interrupt signal is triggered. */
struct FdtInterrupt {
    uint32_t Controller;
    int Line;
    unsigned int Flags;
};

struct FdtResources;

/**
 * @brief Read a shared peripheral interrupt (SPI) description for an Arm GIC.
 *
 * @param provider Resources for the interrupt controller referenced by firmware.
 * @param cells Three 32-bit device-tree values: interrupt type, number, and trigger
 *              settings. The caller must check that all 12 bytes are available.
 * @param interrupt Receives the controller ID, GIC line, and settings on success.
 * @return OS_EOK on success, or OS_ENOTSUPPORTED for an unsupported controller,
 *         interrupt type, number, or trigger setting.
 */
oserr_t
FdtGicInterrupt(
    _In_ const struct FdtResources* provider,
    _In_ const uint8_t* cells,
    _Out_ struct FdtInterrupt* interrupt);

#endif //!__FIRMWARE_INTERRUPT_H__

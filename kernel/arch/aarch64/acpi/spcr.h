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

#ifndef __VALI_AARCH64_ACPI_SPCR_H__
#define __VALI_AARCH64_ACPI_SPCR_H__

#include <os/osdefs.h>

/**
 * @brief Sets up a polled serial console from the ACPI SPCR table, if there is
 * one.
 *
 * Memory and the ACPICA tables must already be initialized. Only the PL011
 * UART is supported, and we keep the baud rate and line settings that firmware
 * left on it. If the table is missing, malformed or describes something we
 * don't support, we continue without a console. No interrupt is registered
 * here, and no other platform resources are set up.
 */
__EXTERN void
Arm64AcpiInitializeConsole(void);

#endif //!__VALI_AARCH64_ACPI_SPCR_H__

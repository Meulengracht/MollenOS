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

#include "platform.h"
#include "private.h"

// The ACPI code fills this in once all the required tables have been checked.
// Only the UART fields are set earlier, when the SPCR console is set up. RPi
// builds use devicetree/platform.c instead, which defines its own copy.
struct Arm64Platform g_arm64Platform;

oserr_t
Arm64PlatformInterruptInitialize(
    _In_ uint32_t interruptCount)
{
    (void)interruptCount;
    return OS_EOK;
}

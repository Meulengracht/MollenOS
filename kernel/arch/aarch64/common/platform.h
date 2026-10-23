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

#ifndef __VALI_AARCH64_PLATFORM_H__
#define __VALI_AARCH64_PLATFORM_H__

#include <os/osdefs.h>

/**
 * @brief Initialize platform-specific interrupt controllers after the GIC.
 *
 * The selected platform implementation may publish additional controllers.
 * The generic implementation has no additional controllers to initialize.
 *
 * @param interruptCount Number of usable interrupt IDs exposed by the GIC.
 * @return OS_EOK or an error that prevents interrupt initialization.
 */
__EXTERN oserr_t
Arm64PlatformInterruptInitialize(
    _In_ uint32_t interruptCount);

#endif

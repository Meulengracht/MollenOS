/**
 * Copyright 2021, Philip Meulengracht
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
 *
 * X86 TSC Driver
 */

#ifndef __X86_TSC_H__
#define __X86_TSC_H__

#include <os/osdefs.h>
#include <time.h>

/**
 * @brief Calibrates the TSC. One fixed timer must be in calibration mode for
 * this to work. The CPU must support TSC, otherwise this will return immediately.
 */
KERNELAPI void KERNELABI
TscInitialize(void);

#endif // !__X86_TSC_H__

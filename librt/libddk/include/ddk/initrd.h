/**
 * Copyright 2017, Philip Meulengracht
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

#ifndef __DDK_INITRD_H__
#define __DDK_INITRD_H__

#include <ddk/ddkdefs.h>

// forward declare this to avoid any vafs includes
struct VaFs;

/**
 * @brief Handles the neccessary filter setup for the OS initrd
 *
 * @param[In] vafs The vafs handle that requires filter setup
 * @return Returns -1 if any errors was detected, 0 otherwise.
 */
DDKDECL(int,
DdkInitrdHandleVafsFilter(
        _In_ struct VaFs* vafs));

#endif //!__DDK_INITRD_H__

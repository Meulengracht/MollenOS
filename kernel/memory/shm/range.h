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
 */

#ifndef __SHM_RANGE_H__
#define __SHM_RANGE_H__

#include <os/osdefs.h>

/**
 * @brief Check whether a byte range fits within a containing range.
 * Uses subtraction to avoid overflow. Empty ranges are allowed, including
 * at the containing range's end. The caller must reject empty ranges when
 * bytes are required.
 *
 * @param rangeOffset First byte of the containing range.
 * @param rangeLength Number of bytes in the containing range.
 * @param offset First byte of the range to check, in the same coordinate space.
 * @param length Number of bytes to check.
 * @return True if the range fits entirely within the containing range.
 */
static inline bool
__SHMRangeContains(
    _In_ size_t rangeOffset,
    _In_ size_t rangeLength,
    _In_ size_t offset,
    _In_ size_t length)
{
    if (offset < rangeOffset) {
        return false;
    }
    offset -= rangeOffset;
    return offset <= rangeLength && length <= rangeLength - offset;
}

#endif //!__SHM_RANGE_H__

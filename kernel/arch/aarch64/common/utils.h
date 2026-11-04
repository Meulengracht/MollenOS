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

#ifndef __AARCH64_UTILS_H__
#define __AARCH64_UTILS_H__

#include <os/osdefs.h>

/**
 * @brief Convert a timer interval to ticks, rounding fractional ticks upward.
 * Cap the interval below half the counter range so the signed timer comparison
 * keeps the programmed deadline in the future when the counter wraps.
 *
 * @param nanoseconds Relative interval until the next scheduler update.
 * @param frequency Nonzero counter frequency in ticks per second.
 * @return Tick interval, capped at INT64_MAX.
 */
static inline uint64_t
__TimerIntervalTicks(
    _In_ uint64_t nanoseconds,
    _In_ uint64_t frequency)
{
    uint64_t seconds = nanoseconds / NSEC_PER_SEC;
    uint64_t remainder = nanoseconds % NSEC_PER_SEC;
    uint64_t whole;
    uint64_t fraction;

    if (seconds > (uint64_t)INT64_MAX / frequency) {
        return INT64_MAX;
    }
    whole = seconds * frequency;

    // Split the frequency so fractional conversion cannot overflow either.
    fraction = remainder * (frequency / NSEC_PER_SEC);
    fraction += (remainder * (frequency % NSEC_PER_SEC) + NSEC_PER_SEC - 1) / NSEC_PER_SEC;
    if (fraction > (uint64_t)INT64_MAX - whole) {
        return INT64_MAX;
    }
    return whole + fraction;
}

#endif //!__AARCH64_UTILS_H__

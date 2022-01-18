/**
 * Copyright 2022, Philip Meulengracht
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
 * Time Definitions & Structures
 * - This header describes the shared time-structure, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#ifndef __TYPES_TIME_H__
#define __TYPES_TIME_H__

enum VaClockSourceType {
    VaClockSourceType_MONOTONIC, // Provides a monotonic tick since the computer was booted
    VaClockSourceType_THREAD,    // Provides a monotonic tick since the calling thread was started.
    VaClockSourceType_PROCESS,   // Provides a monotonic tick since the current process was started.
    VaClockSourceType_HPC        // Provides a high-precision tick counter
};

#endif //!__TYPES_TIME_H__

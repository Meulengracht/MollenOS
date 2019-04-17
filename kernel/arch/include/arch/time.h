/* MollenOS
 *
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
 * along with this program.If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * System Time Interface
 * - Contains the shared kernel time interface
 *   that all sub-layers / architectures must conform to
 */

#ifndef __SYSTEM_INTERFACE_TIME_H__
#define __SYSTEM_INTERFACE_TIME_H__

#include <os/mollenos.h>

/* ArchSynchronizeSystemTime
 * Waits for the system time to change it's counter. This is usefull for the kernel to
 * perform a initial reset of its internal tick counter so it matches with the system time. */
KERNELAPI OsStatus_t KERNELABI
ArchSynchronizeSystemTime(void);

/* ArchGetSystemTime
 * Retrieves the real-time clock for the system. */
KERNELAPI OsStatus_t KERNELABI
ArchGetSystemTime(
    _In_ SystemTime_t* SystemTime);

/* ArchStallProcessorCore
 * Stalls the cpu for the given milliseconds, blocking call. */
KERNELAPI void KERNELABI
ArchStallProcessorCore(
    size_t MilliSeconds);

#endif // !__SYSTEM_INTERFACE_TIME_H__

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
 */

#ifndef __OS_TIME_H__
#define __OS_TIME_H__

#include <os/osdefs.h>
#include <os/types/time.h>

_CODE_BEGIN

/**
 * @brief Puts the calling thread to sleep for the requested duration. The actual time
 * slept is not guaranteed, but will be returned in the remaining value.
 *
 * @param[In]            deadline  The UTC timestamp to sleep untill.
 * @param[Out, Optional] remaining The remaining time if less time was slept than the value in timeout.
 * @return OS_EOK if the sleep was not interrupted. Otherwise returns OsInterrupted.
 */
CRTDECL(oserr_t,
OSSleep(
        _In_      OSTimestamp_t* deadline,
        _Out_Opt_ OSTimestamp_t* remaining));

/**
 * @brief Stalls the current thread for the given duration. It will stall for atleast the duration
 * provided, but can be stalled for longer if the thread is scheduled.
 *
 * @param[In] duration The duration to stall the thread for in nanoseconds.
 * @return Will always succeed.
 */
CRTDECL(oserr_t,
OSStall(
        _In_ UInteger64_t* duration));

/**
 * @brief Reads the current wall clock from the kernel wallclock driver. No guarantee is made to
 * the precision of this time other than second-precision. However the value is in microseconds. The time
 * is represented in microseconds since Jaunary 1, 2020 UTC.
 *
 * @param[Out] time Pointer to where the time will be stored.
 * @return     Returns OS_EOK if the clock was read, otherwise OsNotSupported.
 */
CRTDECL(oserr_t,
OSGetWallClock(
        _In_ OSTimestamp_t* time));

/**
 * @brief Reads the current clock tick for the given clock source type. No guarantees are made for their
 * precision or availability. If the system is in low-precision mode, the return status will be OsNotSupported
 * for the _HPC source.
 *
 * @param[In]  source  The clock source to read the tick for.
 * @param[Out] tickOut Pointer to a large integer value that can hold the current tick value.
 * @return     Returns OS_EOK if the tick was read, otherwise OsNotSupported.
 */
CRTDECL(oserr_t,
OSGetClockTick(
        _In_ enum OSClockSource source,
        _In_ UInteger64_t*       tickOut));

/**
 * @brief Reads the frequency of the clock source type. Use this to calculate the resolution of a given
 * clock source.
 *
 * @param[In]  source       The clock source to read the frequency for
 * @param[Out] frequencyOut Pointer to a large integer value that can hold the frequency value.
 * @return     Returns OS_EOK if the tick was read, otherwise OsNotSupported.
 */
CRTDECL(oserr_t,
OSGetClockFrequency(
        _In_ enum OSClockSource source,
        _In_ UInteger64_t*       frequencyOut));

_CODE_END
#endif //!__OS_TIME_H__

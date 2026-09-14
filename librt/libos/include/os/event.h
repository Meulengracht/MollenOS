/**
 * Copyright 2023, Philip Meulengracht
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

#ifndef __OS_EVENT_H__
#define __OS_EVENT_H__

#include <os/types/handle.h>

#define OSEVENT_LOCK_NONBLOCKING 0x1

_CODE_BEGIN

/**
 * @brief Creates an event object with the specified initial and maximum values.
 * @param initialValue The value assigned to the event when it is created.
 * @param maxValue The maximum value that can be assigned to the event.
 * @param handleOut Receives the handle for the new event.
 * @return OS_EOK on success; otherwise, an error code.
 */
CRTDECL(oserr_t,
OSEvent(
        _In_  unsigned int initialValue,
        _In_  unsigned int maxValue,
        _Out_ OSHandle_t*  handleOut));

/**
 * @brief Creates an event that automatically signals after a timeout.
 * @param timeout The timeout, in milliseconds, before the event is signaled.
 * @param handleOut Receives the handle for the new event.
 * @return OS_EOK on success; otherwise, an error code.
 */
CRTDECL(oserr_t,
OSTimeoutEvent(
        _In_  unsigned int timeout,
        _Out_ OSHandle_t*  handleOut));

/**
 * @brief Waits for an event value and locks the event.
 * @param handle The event handle to lock.
 * @param options Lock behavior flags, including OSEVENT_LOCK_NONBLOCKING.
 * @return OS_EOK when the event is locked; otherwise, an error code.
 */
CRTDECL(oserr_t,
OSEventLock(
        _In_ OSHandle_t*  handle,
        _In_ unsigned int options));

/**
 * @brief Releases an event and increases its available value.
 * @param handle The event handle to release.
 * @param count The number of event units to release.
 * @return OS_EOK on success; otherwise, an error code.
 */
CRTDECL(oserr_t,
OSEventUnlock(
        _In_ OSHandle_t*  handle,
        _In_ unsigned int count));

/**
 * @brief Returns the current value of an event.
 * @param handle The event handle to query.
 * @return The current event value, or a negative error code on failure.
 */
CRTDECL(int,
OSEventValue(
        _In_ OSHandle_t* handle));

_CODE_END
#endif //!__EVENT_H__

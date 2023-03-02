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
 * @brief
 * @param initialValue
 * @param handleOut
 * @param eventOut
 * @return
 */
CRTDECL(oserr_t,
OSEvent(
        _In_  unsigned int initialValue,
        _In_  unsigned int maxValue,
        _Out_ OSHandle_t*  handleOut));

/**
 * @brief
 * @param timeout
 * @param handleOut
 * @return
 */
CRTDECL(oserr_t,
OSTimeoutEvent(
        _In_  unsigned int timeout,
        _Out_ OSHandle_t*  handleOut));

/**
 * @brief
 * @param event
 * @param options
 * @return
 */
CRTDECL(oserr_t,
OSEventLock(
        _In_ OSHandle_t*  handle,
        _In_ unsigned int options));

/**
 * @brief
 * @param event
 * @param count
 * @return
 */
CRTDECL(oserr_t,
OSEventUnlock(
        _In_ OSHandle_t*  handle,
        _In_ unsigned int count));

/**
 * @brief
 * @param handle
 * @return
 */
CRTDECL(int,
OSEventValue(
        _In_ OSHandle_t* handle));

_CODE_END
#endif //!__EVENT_H__

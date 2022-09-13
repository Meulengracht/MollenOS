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

#ifndef __OS_USCHED_ONCE_H__
#define __OS_USCHED_ONCE_H__

#include <os/osdefs.h>
#include <os/usched/mutex.h>

struct usched_once_flag {
    struct usched_mtx mutex;
    int               value;
};

_CODE_BEGIN
/**
 * @brief Calls function func exactly once, even if invoked from several jobs.
 * The completion of the function func synchronizes with all previous or subsequent
 * calls to call_once with the same flag variable.
 * @param flag
 * @param func
 */
CRTDECL(void,
usched_call_once(
    _In_ struct usched_once_flag* flag,
    _In_ void                   (*func)(void)));


_CODE_END
#endif //!__OS_USCHED_ONCE_H__

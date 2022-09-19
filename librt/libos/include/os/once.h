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

#ifndef __OS_ONCE_H__
#define __OS_ONCE_H__

#include <os/osdefs.h>
#include <os/mutex.h>

typedef struct OnceFlag {
    Mutex_t Mutex;
    int     Value;
} OnceFlag_t;

#define ONCE_FLAG_INIT { MUTEX_INIT(MUTEX_PLAIN), 0 }

_CODE_BEGIN
/**
 * @brief Calls function func exactly once, even if invoked from several threads.
 * The completion of the function func synchronizes with all previous or subsequent
 * calls to call_once with the same flag variable.
 * @param flag
 * @param func
 */
CRTDECL(void,
CallOnce(
    _In_ OnceFlag_t* flag,
    _In_ void      (*func)(void)));


_CODE_END
#endif //!__OS_ONCE_H__

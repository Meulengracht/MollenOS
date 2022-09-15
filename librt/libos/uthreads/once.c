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

#include <assert.h>
#include <os/usched/once.h>

void usched_call_once(
    _In_ struct usched_once_flag* flag,
    _In_ void                   (*func)(void))
{
    assert(flag != NULL);
    
    usched_mtx_lock(&flag->mutex);
    if (!flag->value) {
        flag->value = 1;
        func();
    }
    usched_mtx_unlock(&flag->mutex);
}

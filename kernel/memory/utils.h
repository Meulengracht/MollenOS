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

#ifndef __MEMORY_GLOBAL_HANDLE_H__
#define __MEMORY_GLOBAL_HANDLE_H__

#include <debug.h>
#include <handles/global.h>

/**
 * @brief Drops a global handle reference and logs unexpected failures.
 *
 * OS_EINCOMPLETE is expected when other references still keep the handle alive.
 */
static inline void
__ReleaseMemoryHandleReference(
    _In_ uuid_t       handle,
    _In_ const char*  caller)
{
    oserr_t status = ReleaseGlobalHandle(handle);

    if (status != OS_EOK && status != OS_EINCOMPLETE) {
        ERROR("%s: ReleaseGlobalHandle %u failed: %u", caller, handle, status);
    }
}

#endif //!__MEMORY_GLOBAL_HANDLE_H__

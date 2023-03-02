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

#ifndef __OS_HANDLE_H__
#define __OS_HANDLE_H__

#include <os/types/handle.h>

/**
 * @brief Creates a new system handle. The handle itself is global and can
 * be referred to globally, however it may in most cases be useless if not
 * properly shared. In most instances system handles are only used locally
 * for each process. If a handle needs to be shared, then read the relevant
 * documentation for each handle type on how to use and share.
 * @param type The type of system handle that needs to be created.
 * @param payload An implementation specific payload that is paired with the handle.
 * @param handle An OSHandle structure that describes this system handle. This
 *               is filled on a succesful operation.
 * @return OS_EOK if the handle was succesfully created.
 */
CRTDECL(oserr_t,
OSHandleCreate(
        _In_ enum OSHandleType type,
        _In_ void*             payload,
        _In_ struct OSHandle*  handle));

/**
 * @brief Constructs a new handle based on an existing system handle. This is useful
 * for wrapping some kernel API calls in a new OSHandle structure.
 * @param id The global system handle ID.
 * @param type The type of the global system handle.
 * @param payload An implementation specific payload that is paired with the handle.
 * @param handle An OSHandle structure that describes this system handle. This
 *               is filled on a succesful operation.
 * @return OS_EOK if the handle was succesfully created.
 */
CRTDECL(oserr_t,
OSHandleWrap(
        _In_ uuid_t            id,
        _In_ enum OSHandleType type,
        _In_ void*             payload,
        _In_ struct OSHandle*  handle));

/**
 * @brief Frees a previously allocated system handle. This makes the handle invalid
 * in the global scope, and should any other actors refer to this handle, their handles
 * may become invalid. How the freeing and how this affects the handle may vary based on
 * the handle type. This should only be used in direct conjungtion with OSHandleCreate.
 * Some handles have multiple references to them in the global scope, and thus destroying
 * them through here will only reduce the reference, and allow the resources to continue
 * as long as there are remaining references.
 * @param id The global ID of the system handle.
 */
CRTDECL(void,
OSHandleDestroy(
        _In_ uuid_t id));

/**
 * @brief
 * @param id
 * @param handle
 * @return
 */
CRTDECL(oserr_t,
OSHandlesFind(
        _In_ uuid_t           id,
        _In_ struct OSHandle* handle));

#endif //!__OS_HANDLE_H__

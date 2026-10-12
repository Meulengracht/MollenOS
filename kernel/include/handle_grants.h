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
 *
 * Resource Handle Grant Interface
 * - Implementation of the resource handle interface. This provides access
 *   control of resource handles.
 *
 */

#ifndef __HANDLE_GRANTS_H__
#define __HANDLE_GRANTS_H__

#include <os/osdefs.h>
#include <handle.h>

typedef enum HandleGrantRights {
    HandleGrantOwner = 0x1,
    HandleGrantRead  = 0x2,
    HandleGrantWrite = 0x4,
} HandleGrantRights_t;

// Forward declarations
struct HandleGrantTable;

/**
 * @brief Sets up and initializes the handle grant table for the current 
 * thread cluster. This is only used by the memory subsystem that initalies
 * the thread cluster context.
 */
KERNELAPI void KERNELABI
HandleGrantTableInitialize(
    _In_ struct HandleGrantTable* table);

/**
 * @brief Close all handle references associated with the grant table. This
 * makes the grant table unusable. This is automically called by Destroy too.
 */
KERNELAPI void KERNELABI
HandleGrantTableClose(
    _In_ struct HandleGrantTable* table);

/**
 * @brief Destroys and cleans up the handle grant table for the current thread cluster.
 * This releases all handles associated with the grants.
 */
KERNELAPI void KERNELABI
HandleGrantTableDestroy(
    _In_ struct HandleGrantTable* table);

/**
 * @brief Adds a new handle grant to the handle grant table. This will usually
 * be called right after a new handle is created, at this point we do not increase
 * the reference count of the handle. Instead ownership is now assumed by the 
 * grant table (i.e. the thread cluster).
 */
KERNELAPI oserr_t KERNELABI
HandleGrantAdd(
    _In_ uuid_t              id,
    _In_ HandleGrantRights_t rights);

/**
 * @brief Acquires a handle grant for the specified resource. This will check
 * if the current thread cluster has the requested rights for the handle. If
 * successful, the resource associated with the handle will be returned in
 * `resourceOut`.
 */
KERNELAPI oserr_t KERNELABI
HandleGrantAcquire(
    _In_ uuid_t              id,
    _In_ HandleType_t        handleType,
    _In_ HandleGrantRights_t rights,
    _In_ void**              resourceOut);

/**
 * @brief Releases a handle grant for the specified resource.
 */
KERNELAPI oserr_t KERNELABI
HandleGrantRelease(
    _In_ uuid_t id);

#endif //! __HANDLE_GRANTS_H__

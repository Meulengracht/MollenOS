/**
 * MollenOS
 *
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
 * Private Handle Interface
 * - Structures and functions that are shared between handle.c,
 *   handle_provider.c and handle_set.c. They must not be used from anywhere
 *   else in the kernel.
 */

#ifndef __HANDLE_PRIVATE_H__
#define __HANDLE_PRIVATE_H__

#include <handles/grants.h>

struct __ProviderResource;

/**
 * @brief A service registration, created by HandleProviderRegister. When the
 * last reference to one of the resources of the service is closed, the resource
 * is put on the Pending list, and the queue of the service is marked with
 * IOSETIN, so the service knows to clean up its own state for the resource.
 * ID, GroupID and QueueID never change after the registration has been created.
 * Open, Pending and PendingTail must only be used while holding g_providerLock.
 * Each resource holds a global reference to its registration, so the memory of
 * the registration is kept until all of its resources are gone, even if the
 * service has closed it.
 */
struct __HandleProvider {
    uuid_t                     ID;          // The handle ID of the registration
    uuid_t                     GroupID;     // The security group of the service
    uuid_t                     QueueID;     // The queue we post to, we hold a reference on it
    bool                       Open;        // Cleared when the service closes it, or exits
    struct __ProviderResource* Pending;     // First resource waiting for cleanup
    struct __ProviderResource* PendingTail; // Last resource waiting for cleanup
};

/**
 * @brief A resource of a service. Clients never get the resource itself, only
 * references to it (struct __ProviderReference), each with their own access.
 * Provider and ID never change once HandleProviderCreate has finished, so
 * they can be read without holding g_providerLock, as long as the caller holds
 * a reference that keeps the resource alive. The other members must only be
 * used while holding g_providerLock.
 * Published is set once HandleProviderCreate has finished, and cleared again by
 * HandleProviderCancelCreate if the new ID never reached the service. When
 * References reaches 0, the resource is freed right away if Published is not
 * set, or if the service has closed its registration. Otherwise it is put on the
 * Pending list of the registration, so the service is told about it.
 */
struct __ProviderResource {
    struct __HandleProvider*   Provider; // The service the resource belongs to
    struct __ProviderResource* Next;         // Next resource on the Pending list
    uuid_t                     ID;           // The service's ID for the resource
    unsigned int               References;   // Number of references to the resource
    bool                       Published;    // Cleared if the create is cancelled
};

/**
 * @brief A reference to a resource of a service. Each handle of the type
 * HandleTypeProviderReference points to one of these. Each reference has its
 * own access mask, so two references to the same resource can allow different
 * things. Neither member changes once the reference has been created.
 */
struct __ProviderReference {
    struct __ProviderResource* Resource;   // The resource this is a reference to
    uint32_t                   AccessMask; // What the reference allows
};

/**
 * @brief Acquires a global handle without knowing its type in advance.
 * The type of the handle is returned in typeOut, and a pointer to the resource
 * is returned in resourceOut if it is not NULL. The caller must release the
 * handle again with DestroyHandle once done.
 *
 * @param handleId The ID of the handle to acquire.
 * @param typeOut Receives the type of the handle.
 * @param resourceOut Receives a pointer to the resource, or NULL if not needed.
 * @return OS_EOK, OS_ENOENT if the handle does not exist, OS_EUNKNOWN if the
 *         handle is of a different type than expected, or OS_EOVERFLOW if the
 *         reference count cannot be increased.
 */
oserr_t
AcquireGlobalHandleUntyped(
    _In_  uuid_t        handleId,
    _Out_ HandleType_t* typeOut,
    _Out_ void**        resourceOut);

/**
 * @brief Adds a temporary count to a grant in the grant table of the current
 * process. AddHandleToSet uses this to keep the grant open while it adds a
 * subscription that is tied to it. Unlike AcquireHandle this gives no access
 * to the resource, and no pointer to it is returned. The caller must drop the
 * count again with DestroyHandle, from the same process, once the subscription
 * has been added or the add has failed.
 *
 * @param handle The ID of the grant in the grant table of the current process.
 * @return OS_EOK, OS_ENOENT if the current process has no grant for it (this is
 *         also returned for the kernel, which has no grant table), or
 *         OS_EOVERFLOW if the count can't go any higher.
 */
oserr_t
HandleSetsReferenceGrant(
    _In_ uuid_t handle);

/**
 * @brief Removes the subscriptions in handle sets that are tied to a grant which
 * is being closed. Only handle.c should call this, as the locking rules follow
 * how grants are closed in there.
 * When a single grant is closed, the caller must hold the lock of the grant
 * table, and the grant must already be removed from the table, but its global
 * reference not yet dropped. When a process exits, handle must be UUID_INVALID,
 * and every subscription added through the table is removed. In this case no
 * lock is needed, as nothing else can use the table anymore, but it must be
 * called before the grants in it are released.
 * If the grant is for a service registration, the subscriptions the service has
 * on the resource IDs of its own resources are removed as well.
 *
 * @param table  The grant table of the process.
 * @param handle The grant that is being closed, or UUID_INVALID when the process
 *               exits.
 */
void
HandleSetsRemoveGrant(
    _In_ HandleGrantsTable_t* table,
    _In_ uuid_t              handle);

#endif //!__HANDLE_PRIVATE_H__

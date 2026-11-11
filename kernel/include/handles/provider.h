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
 * Handle Service Interface
 * - Services use this interface to give other applications access to their
 *   resources through OS handles.
 */

#ifndef __HANDLES_PROVIDER_H__
#define __HANDLES_PROVIDER_H__

#include <handles/types.h>

DECL_STRUCT(MSContextSecurity);

/**
 * @brief Register the current group as a handle service.
 * Each call creates a separate registration. When the last grant to it is
 * closed, or the group ends, every resource created through it becomes invalid.
 *
 * @param notificationQueueID Queue the caller has a grant for. The queue is marked
 *                            with IOSETIN when resources are waiting for cleanup.
 * @param registrationOut     Receives the new registration. It cannot be granted
 *                            to other groups.
 * @return OS_EOK, or a queue, permission or allocation error.
 */
oserr_t
HandleProviderRegister(
    _In_  uuid_t  notificationQueueID,
    _Out_ uuid_t* registrationOut);

/**
 * @brief Create a new resource and its first restricted reference.
 * If registrationID is UUID_INVALID, a plain notification handle is created
 * instead.
 *
 * @param registrationID Registration the caller has a grant for, or UUID_INVALID.
 * @param accessMask     Access allowed through the reference. It is fixed once the
 *                       reference is created, and must be zero for notification
 *                       handles.
 * @param referenceOut   Receives the reference ID. The service also uses this ID
 *                       to identify the resource, and it never changes.
 * @return OS_EOK, or a registration, access or allocation error.
 */
oserr_t
HandleProviderCreate(
    _In_    uuid_t   registrationID,
    _In_    uint32_t accessMask,
    _Out_   uuid_t*  referenceOut);

/**
 * @brief Import a token and create a restricted reference to its resource.
 * Each import creates a new reference with its own access mask, even if the
 * receiver already has a reference to the same resource. The new reference is
 * added to the grant table of the receiver, and the calling group gets no grant
 * for it unless it is the receiver. If another group is given, the kernel caller
 * must have checked that the import is allowed for it, and must keep it alive
 * until it is done with the new reference (for instance, if it has to remove it
 * again because the ID could not be copied to userspace).
 * A single-use token is used up by GrantTokenImport before the reference is
 * created, so it stays used up even if creating the reference fails.
 *
 * @param token        Kernel copy of the token to import.
 * @param receiver     The group that gets the new reference, or NULL for the
 *                     calling group.
 * @param referenceOut Receives the new reference ID. Unchanged on failure.
 * @return OS_EOK, a GrantTokenImport error, OS_ECANCELLED if the service has
 *         closed, or an allocation error from creating the reference.
 */
oserr_t
HandleProviderImport(
    _In_  const GrantToken_t*  token,
    _In_  MSContextSecurity_t* receiver,
    _Out_ uuid_t*              referenceOut);

/**
 * @brief Check that another group has a grant for a reference, and that the
 * reference allows the requested access. No reference is kept after the call.
 *
 * @param registrationID Registration that belongs to the current security group.
 * @param groupID        Full group ID of the client, taken from the authenticated
 *                       IPC message. Never pass a value from the RPC arguments.
 * @param referenceID    The client's restricted reference ID.
 * @param accessMask     Access the service needs for this request.
 * @param resourceOut    Receives the service's ID for the resource on success.
 * @return OS_EOK, or a registration, grant, permission or group error.
 */
oserr_t
HandleProviderValidate(
    _In_    uuid_t   registrationID,
    _In_    uint64_t groupID,
    _In_    uuid_t   referenceID,
    _In_    uint32_t accessMask,
    _Out_   uuid_t*  resourceOut);

/**
 * @brief Copy the IDs of resources waiting for cleanup to userspace. Entries are
 * only removed from the queue once both copies have succeeded. On failure
 * nothing is removed, and the contents of userIDs must be ignored.
 *
 * @param registrationID Registration that belongs to the current security group.
 * @param userIDs        Userspace array that receives the service IDs.
 * @param capacity       Number of entries in userIDs. Must be nonzero.
 * @param userCount      Userspace pointer that receives the number of IDs copied.
 * @return OS_EOK, or a registration, copy or allocation error.
 */
oserr_t
HandleProviderGetEvents(
    _In_    uuid_t    registrationID,
    _Out_   uuid_t*   userIDs,
    _In_    uint32_t  capacity,
    _Out_   uint32_t* userCount);

/**
 * @brief Check that a reference allows the requested token scope, and add a
 * global reference for the new token. Every scope flag must also be set in the
 * access mask of the reference.
 *
 * @param referenceID Reference the caller has a grant for.
 * @param options     Kernel copy of the token options. Both data words must be zero.
 * @param typeOut     Receives the kernel handle type of the reference on success.
 * @return OS_EOK, OS_ECANCELLED if the service has ended, or a grant or permission
 *         error.
 */
oserr_t
HandleProviderReferenceForExport(
    _In_    uuid_t                     referenceID,
    _In_    const GrantTokenOptions_t* options,
    _Out_   HandleType_t*              typeOut);

/**
 * @brief Skip the cleanup notification for a resource when its first reference
 * never reached the creator. This is used when copying the new ID to userspace
 * fails.
 *
 * @param referenceID Reference returned by HandleProviderCreate. Plain
 *                    notification handles are ignored.
 */
void
HandleProviderCancelCreate(
    _In_    uuid_t referenceID);

#endif //!__HANDLES_PROVIDER_H__

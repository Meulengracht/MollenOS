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
#include <os/types/grants.h>

/**
 * @brief Registers the calling group as a handle service. Each call creates a
 * new registration, so a group can have several and close them one by one.
 * Registrations cannot be transferred to other groups. When the last reference
 * to a registration is closed, or the security group ends, all resources
 * created through it become invalid.
 * When cleanup is pending, the queue receives IOSETIN with data.handle set to
 * the registration ID. The queue is set up by this call, so there is no need to
 * call OSNotificationQueueCtrl. The kernel keeps a reference to the queue for
 * as long as the registration exists, so closing the local queue handle does
 * not end the registration. Wakeups that are already queued may still arrive
 * after the registration has been closed.
 *
 * @param notificationQueue Notification queue the caller has access to. Used for
 *                          cleanup wakeups.
 * @param registrationOut   Receives the registration handle.
 * @return OS_EOK, or a queue, permission or allocation error.
 */
CRTDECL(oserr_t,
OSHandleRegisterProvider(
    _In_  OSHandle_t* notificationQueue,
    _Out_ OSHandle_t* registrationOut));

/**
 * @brief Creates a new resource for a handle service, and returns the first
 * reference to it. The returned ID is also the ID used for the resource in
 * cleanup notifications, and it never changes.
 * The service must close this first reference once it has given clients access
 * to the resource. If the service keeps it open, for example in an object
 * table, the resource will never be cleaned up.
 *
 * @param registration Service registration, or NULL to create a plain
 *                     notification handle.
 * @param accessMask   Access allowed through the reference. Must be zero when
 *                     registration is NULL.
 * @param resourceOut  Receives the first reference. Notification handles cannot
 *                     be exported through grant tokens.
 * @return OS_EOK, or a registration, access or allocation error.
 */
CRTDECL(oserr_t,
OSHandleCreate(
    _In_  OSHandle_t* registration,
    _In_  uint32_t    accessMask,
    _Out_ OSHandle_t* resourceOut));

/**
 * @brief Imports a handle service token, and creates a new restricted reference
 * to its resource. Each import gives a separate reference with the access mask
 * of the token, so importing more than once never combines permissions.
 * Revoking the token stops future imports, but references that have already
 * been imported stay open.
 * A single-use token is used up by the import, before the reference is created.
 * So the token stays used up even if creating the reference fails, or if the
 * result cannot be copied back (in this case the new reference is closed).
 *
 * @param token     Token with its allowed receiver and the access mask for the
 *                  new reference. The access mask cannot be changed.
 * @param handleOut Receives the new reference. Release it with OSHandleRelease.
 * @return OS_EOK, or a token, registration or allocation error. OS_ECANCELLED
 *         means the token has been revoked or used up, or the service has ended.
 */
CRTDECL(oserr_t,
OSHandleImport(
    _In_  const GrantToken_t* token,
    _Out_ OSHandle_t*         handleOut));

/**
 * @brief Checks that the sender of a request has a grant for a reference to one
 * of this service's resources, and that the reference allows the access needed.
 * No reference is kept after the call returns. This means the service must
 * synchronize its object lookup and any active work with cleanup. If the object
 * was removed before the lookup, the request must fail. Work that is already
 * using the object must finish before the service frees its state.
 *
 * @param registration   Registration that belongs to the calling security group.
 * @param callerGroupID  Group ID of the sender, taken from the kernel IPC
 *                       metadata. Never pass a value from the RPC arguments.
 * @param referenceID    Reference given by the client.
 * @param requiredAccess Access flags needed by the request. Zero only checks that
 *                       the client has the reference.
 * @param resourceIDOut  Receives the ID the service uses for the resource.
 * @return OS_EOK, OS_ENOENT if the client has no grant for the reference,
 *         OS_EPERMISSIONS if the access is not allowed or the resource belongs
 *         to another service, or OS_ECANCELLED if the service has ended.
 */
CRTDECL(oserr_t,
OSHandleValidate(
    _In_  OSHandle_t* registration,
    _In_  uint64_t    callerGroupID,
    _In_  uuid_t      referenceID,
    _In_  uint32_t    requiredAccess,
    _Out_ uuid_t*     resourceIDOut));

/**
 * @brief Reads the IDs of resources waiting for cleanup. The IDs are only removed
 * from the queue once the whole batch has been copied.
 * The queue given at registration gets one wakeup when cleanup becomes pending.
 * Reading the IDs uses up that wakeup, and another one is only posted if entries
 * are still pending after a partial or failed read. An old wakeup can give an
 * empty batch. Callers can also just call this until countOut is zero.
 * Once the IDs are delivered, the service is responsible for the cleanup and any
 * retries. No acknowledgement is needed. The IDs only identify the service's
 * objects, and they cannot be used as handles.
 *
 * @param registration Registration that belongs to the calling security group.
 * @param resourceIDs  Receives up to capacity IDs. Ignore all output on failure.
 * @param capacity     Number of entries in resourceIDs. Must be nonzero.
 * @param countOut     Receives the number of IDs. Zero means nothing is pending.
 * @return OS_EOK, or a registration, permission or buffer error. On failure no
 *         IDs are removed, even if part of the output was written.
 */
CRTDECL(oserr_t,
OSHandleReadCleanup(
    _In_  OSHandle_t* registration,
    _Out_ uuid_t*     resourceIDs,
    _In_  uint32_t    capacity,
    _Out_ uint32_t*   countOut));

/**
 * @brief Constructs a new handle based on an existing system handle. This is useful
 * for wrapping some kernel API calls in a new OSHandle structure.
 * 
 * @param id The global system handle ID.
 * @param type The type of the global system handle.
 * @param payload An implementation specific payload that is paired with the handle.
 * @param ownership Whether or not this OSHandle has ownership of the global system handle.
 * @param handle An OSHandle structure that describes this system handle. This
 *               is filled on a succesful operation.
 * @return OS_EOK if the handle was succesfully created.
 */
CRTDECL(oserr_t,
OSHandleWrap(
        _In_ uuid_t            id,
        _In_ enum OSHandleType type,
        _In_ void*             payload,
        _In_ bool              ownership,
        _In_ struct OSHandle*  handle));

/**
 * @brief Frees a previously allocated system handle. This makes the handle invalid
 * in the global scope, and should any other actors refer to this handle, their handles
 * may become invalid. How the freeing and how this affects the handle may vary based on
 * the handle type. This should only be used in direct conjungtion with OSHandleCreate.
 * Some handles have multiple references to them in the global scope, and thus destroying
 * them through here will only reduce the reference, and allow the resources to continue
 * as long as there are remaining references.
 * 
 * This compatibility operation cannot report cleanup failure. It retains the
 * owner and logs a warning on failure; use OSHandleRelease when retry is needed.
 * @param handle Owning slot, cleared after successful release.
 */
CRTDECL(void,
OSHandleDestroy(
        _In_ struct OSHandle* handle));

/**
 * @brief Release one local reference, retaining ownership if final cleanup fails.
 * Final cleanup runs outside the registry lock. Once it starts, find/acquire
 * refuse this handle until cleanup succeeds; retry release using the same slot.
 * Calls using an owning slot must be serialized. Copying a slot does not acquire
 * another reference; use OSHandlesAcquire before releasing independent aliases.
 * 
 * @param handle Owning slot, cleared on success. NULL/empty slots are harmless.
 * @return OS_EOK, OS_EBUSY during concurrent release, or cleanup/lookup error.
 */
CRTDECL(oserr_t,
OSHandleRelease(
        _InOut_ struct OSHandle* handle));

/**
 * @brief Acquires an additional reference to a system handle.
 * @param handle Handle whose reference should be acquired.
 * @return OS_EOK on success; otherwise, an error code.
 */
CRTDECL(oserr_t,
OSHandlesAcquire(
        _In_ struct OSHandle* handle));

/**
 * @brief Find a borrowed local handle description by its global identifier.
 * Independent SHM attachments may share an ID. This ID-only lookup reports
 * OS_EEXISTS when ambiguous; use the owning slot returned by SHMAttach instead.
 * SHM lookup scans the local registry; ordinary handles retain indexed lookup.
 * 
 * @param id Global system handle identifier.
 * @param handle Receives the handle description.
 * @return OS_EOK, OS_EEXISTS, OS_EBUSY during cleanup, or OS_ENOENT.
 */
CRTDECL(oserr_t,
OSHandlesFind(
        _In_ uuid_t           id,
        _In_ struct OSHandle* handle));

#endif //!__OS_HANDLE_H__

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

#ifndef __DMA_ATTACHMENT_H__
#define __DMA_ATTACHMENT_H__

#include <os/types/dma_attachment.h>

// Forward declarations for opaque types
DECL_STRUCT(DMAManager);
DECL_STRUCT(DMAAttachmentRegistration);
DECL_STRUCT(DMABindingRegistration);

/**
 * @brief Copies of pending events and the sequence numbers used to acknowledge them.
 * The batch contains no attachment pointers. Keep it unchanged and hold a manager
 * operation reference until the snapshot, user copies and finish call are complete.
 * Entries and Sequences use matching indices from zero through Count - 1.
 */
typedef struct DMAAttachmentEventBatch {
    uint32_t               Count;
    OSDmaAttachmentEvent_t Entries[DMA_MANAGER_MAX_EVENTS];
    uint64_t               Sequences[DMA_MANAGER_MAX_EVENTS];
} DMAAttachmentEventBatch_t;

/**
 * @brief Prepare a manager grant without enabling administration.
 * Requires a current security context and a syscall check of
 * CAPABILITY_DEVICEIO_MANAGEMENT. The registry and returned operation each hold
 * a separate reference, in addition to the service grant.
 *
 * @param managerOut Receives an operation reference. Release it after publish or cancel.
 * @param handleOut Receives the service handle to copy to the caller.
 * @return OS_EOK, OS_EBUSY if a manager is already registered, or a security-context
 * or allocation error. Failure leaves the outputs unchanged and adds no registry entry.
 */
KERNELABI oserr_t KERNELAPI
DMAManagerPrepareCreate(
    _Out_ DMAManager_t** managerOut,
    _Out_ uuid_t*        handleOut);

/**
 * @brief Enable a prepared manager after its handle was copied to the caller.
 *
 * @param manager Prepared manager held by an operation reference.
 * @return OS_EOK or OS_ECANCELLED if its grant or security group has closed. On failure,
 * call DMAManagerCancelCreate before releasing the operation reference.
 */
KERNELABI oserr_t KERNELAPI
DMAManagerPublish(
    _In_ DMAManager_t* manager);

/**
 * @brief Close a manager whose creation failed and release its original service grant.
 * Call only after failed creation. The manager closes immediately, even if temporary
 * grant references delay the close callback. The operation reference remains held.
 *
 * @param manager Prepared manager, including one closed during publication.
 */
void
DMAManagerCancelCreate(
    _In_ DMAManager_t* manager);

/**
 * @brief Reference a live manager after checking the caller's grant and full
 * security-group identity.
 *
 * @param handle Manager handle supplied by the service.
 * @param managerOut Receives an independent operation reference only on success.
 * @return Grant or handle-type errors, OS_EPERMISSIONS if the security group is
 * missing or different, OS_EBUSY before publication, or OS_ECANCELLED after the
 * service or grant is lost.
 */
KERNELABI oserr_t KERNELAPI
DMAManagerReference(
    _In_  uuid_t         handle,
    _Out_ DMAManager_t** managerOut);

/**
 * @brief Release an operation reference and clear its pointer, including after close.
 * Requires all manager locks to be released. Publish or cancel any attachment
 * update before calling.
 *
 * @param manager Operation reference to release. Does nothing if *manager is NULL.
 */
KERNELABI void KERNELAPI
DMAManagerRelease(
    _InOut_ DMAManager_t** manager);

/**
 * @brief Check a copied attachment description and prepare a new registration or retry.
 * Requires a manager operation reference until publish or cancel finishes.
 * Parameters must stay unchanged in kernel memory until this call returns.
 * On success, UpdateLock stays held until publication succeeds or cancellation
 * finishes. The same thread must finish this update before starting another.
 *
 * @param manager Live manager referenced by the caller.
 * @param parameters Complete fixed-width description, including unused entries.
 * @param registrationOut Receives the prepared operation only on success.
 * @param attachmentOut Receives its new or existing ID only on success.
 * @return Validation or allocation errors, OS_EBUSY for conflicting state,
 * OS_ENOTSUPPORTED at the entry limit, OS_EOVERFLOW when IDs or event sequences
 * are exhausted, or OS_ECANCELLED after manager close. Failure releases temporary
 * resources.
 */
KERNELABI oserr_t KERNELAPI
DMAManagerPrepareAttachment(
    _In_  DMAManager_t*                      manager,
    _In_  const OSDmaAttachmentParameters_t* parameters,
    _Out_ DMAAttachmentRegistration_t**      registrationOut,
    _Out_ OSDmaAttachmentId_t*               attachmentOut);

/**
 * @brief Publish an attachment and its group changes after copying its ID to the caller.
 *
 * @param registration Prepared operation. Success frees it, clears the pointer and
 * releases UpdateLock.
 * @return OS_EOK or a state or event error. On failure, cancel the prepared operation.
 */
KERNELABI oserr_t KERNELAPI
DMAManagerPublishAttachment(
    _InOut_ DMAAttachmentRegistration_t** registration);

/**
 * @brief Cancel a prepared attachment registration and release UpdateLock.
 * Requires the caller's manager reference until cancellation finishes.
 * Cancelling an identical retry leaves the existing attachment in place.
 *
 * @param registration Prepared operation, cleared on return. Does nothing if
 * *registration is NULL.
 */
KERNELABI void KERNELAPI
DMAManagerCancelAttachment(
    _InOut_ DMAAttachmentRegistration_t** registration);

/**
 * @brief Remove an attachment and rebuild its recovery group.
 *
 * @param manager Live manager held by an operation reference.
 * @param attachmentID Nonzero manager-local ID.
 * @return OS_EOK, OS_ENOENT, OS_EINVALPARAMS for zero, OS_EBUSY for a pending
 * event or current binding in the affected group, OS_EOVERFLOW, OS_EOOM or
 * OS_ECANCELLED. A closed binding blocks removal until its final reference is released.
 */
KERNELABI oserr_t KERNELAPI
DMAManagerRemoveAttachment(
    _In_ DMAManager_t* manager,
    _In_ uint64_t      attachmentID);

/**
 * @brief Copy up to DMA_MANAGER_MAX_EVENTS pending events, leaving them queued.
 *
 * @param manager Referenced manager.
 * @param capacity Number of available output entries. Must be positive and is capped
 * at DMA_MANAGER_MAX_EVENTS.
 * @param batchOut Receives the event copies on success. The batch may be empty.
 * @return OS_EOK, OS_EINVALPARAMS for zero capacity or OS_ECANCELLED after close.
 * A failed snapshot sends another notification if the manager is live and events remain.
 */
KERNELABI oserr_t KERNELAPI
DMAManagerSnapshotEvents(
    _In_  DMAManager_t*              manager,
    _In_  uint32_t                   capacity,
    _Out_ DMAAttachmentEventBatch_t* batchOut);

/**
 * @brief Acknowledge copied events that are unchanged and notify if events remain.
 * Call after every successful snapshot, even when copying outputs failed.
 *
 * @param manager Same referenced manager used for the snapshot.
 * @param batch Unchanged batch returned by the snapshot call.
 * @param outputsCopied True only after both the event array and count were copied successfully.
 * @return OS_EOK or OS_ECANCELLED after close. Events are removed only if both
 * output copies succeeded and their sequences still match. Events whose sequence
 * is exhausted stay queued.
 */
KERNELABI oserr_t KERNELAPI
DMAManagerFinishEvents(
    _In_ DMAManager_t*                    manager,
    _In_ const DMAAttachmentEventBatch_t* batch,
    _In_ bool                             outputsCopied);

/**
 * @brief Prepare a new receiver grant or reference an identical live binding.
 * Requires a live manager held by an operation reference. Parameters must stay
 * unchanged in kernel memory until return. On success, UpdateLock stays held
 * until publication succeeds or cancellation finishes. The same thread must
 * finish this update before starting another.
 *
 * @param manager Manager referenced by the administrative caller.
 * @param parameters Copied, authenticated binding request.
 * @param registrationOut Receives the prepared operation only on success.
 * @param bindingOut Receives the new or existing handle and generation.
 * @return OS_EOK or a validation, permission, state or allocation error.
 * Failure leaves outputs unchanged and releases temporary resources.
 */
KERNELABI oserr_t KERNELAPI
DMAManagerPrepareBinding(
    _In_  DMAManager_t*                manager,
    _In_  const OSDmaBindParameters_t* parameters,
    _Out_ DMABindingRegistration_t**   registrationOut,
    _Out_ OSDmaBindingResult_t*        bindingOut);

/**
 * @brief Publish a prepared binding after copying the result to the caller.
 * Checks the manager and binding state again. For a retry, the grant must still be live.
 *
 * @param registration Prepared operation. Success frees it, clears the pointer and
 * releases UpdateLock.
 * @return OS_EOK or OS_ECANCELLED. On failure, cancel the prepared operation.
 */
KERNELABI oserr_t KERNELAPI
DMAManagerPublishBinding(
    _InOut_ DMABindingRegistration_t** registration);

/**
 * @brief Cancel a prepared binding and release UpdateLock.
 * Closes a new grant in the receiver's group. Cancelling an identical retry leaves
 * the existing grant unchanged. Releases UpdateLock before releasing references.
 *
 * @param registration Prepared operation, cleared on return. Does nothing if
 * *registration is NULL.
 */
KERNELABI void KERNELAPI
DMAManagerCancelBinding(
    _InOut_ DMABindingRegistration_t** registration);

#endif //!__DMA_ATTACHMENT_H__

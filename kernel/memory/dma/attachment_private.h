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

#ifndef __DMA_ATTACHMENT_PRIVATE_H__
#define __DMA_ATTACHMENT_PRIVATE_H__

#include <dma/attachment.h>
#include <ds/list.h>
#include <mutex.h>

DECL_STRUCT(MSContextSecurity);
DECL_STRUCT(DMABinding);
DECL_STRUCT(DMAAttachment);

enum DMAManagerState {
    DMAManagerPreparing,
    DMAManagerLive,
    DMAManagerClosed
};

/**
 * @brief Registered attachments and increasing ID counters.
 * Requires the manager's Lock for access.
 */
typedef struct DMAAttachmentRegistry {
    list_t   Attachments;
    uint32_t Count;
    uint64_t NextAttachmentID;
    uint64_t NextRecoveryGroupID;
    uint64_t NextBindingGeneration;
} DMAAttachmentRegistry_t;

/**
 * @brief Manager grant and state, with registered attachments and pending events.
 * Lock protects state and registry data. UpdateLock allows one prepared change
 * at a time, including while its result is copied to the caller. Closing does
 * not take UpdateLock. The manager keeps no permanent reference to the service's
 * security context, allowing group exit to close the grant.
 */
typedef struct DMAManager {
    uuid_t                  ID;
    uint64_t                Principal;
    enum DMAManagerState    State;
    Mutex_t                 Lock;
    Mutex_t                 UpdateLock;
    DMAAttachmentRegistry_t Registry;
    list_t                  PendingEvents;
} DMAManager_t;

/**
 * @brief Pending event data, its queue link and sequence number.
 * Removing an event from the queue does not reset Sequence. At exhaustion, the
 * event stays pending permanently so an old batch cannot acknowledge a later
 * change. The manager's Lock protects all fields.
 */
typedef struct DMAAttachmentEventState {
    element_t              Link;
    OSDmaAttachmentEvent_t Value;
    uint64_t               Sequence;
    bool                   Queued;
    bool                   Exhausted;
} DMAAttachmentEventState_t;

enum DMABindingState {
    // The binding is not published. The attachment does not yet point to it.
    DMABindingPreparing,
    DMABindingLive,
    // The grant cannot be used again. References may still delay the idle event.
    DMABindingClosed
};

/**
 * @brief Current binding and the last published request for this attachment.
 * Current does not keep the binding alive. The final binding callback clears it
 * under Lock, leaving the request history intact. Principal is zero until the
 * first request is published. Publication requires UpdateLock, and the manager's
 * Lock protects every field.
 */
typedef struct DMAAttachmentBindingState {
    DMABinding_t* Current;
    uint64_t      Principal;
    uint64_t      RequestId;
    uint64_t      Generation;
} DMAAttachmentBindingState_t;

/**
 * @brief Receiver's binding grant and a separate reference to its manager.
 * Attachment stays NULL until publication. Changes to binding state require
 * the manager's Lock.
 */
typedef struct DMABinding {
    uuid_t               ID;
    DMAManager_t*        Manager;
    DMAAttachment_t*     Attachment;
    uint64_t             Principal;
    uint64_t             RequestId;
    uint64_t             Generation;
    enum DMABindingState State;
} DMABinding_t;

/**
 * @brief Prepared binding and references needed to publish or cancel it.
 * UpdateLock stays held. Receiver holds a temporary security-context reference,
 * and Binding holds an operation reference. CreatesBinding selects a new grant
 * rather than an identical live retry, even before the grant is allocated.
 * The caller must keep its manager operation reference until the operation finishes.
 */
typedef struct DMABindingRegistration {
    DMAManager_t*        Manager;
    DMAAttachment_t*     Attachment;
    DMABinding_t*        Binding;
    MSContextSecurity_t* Receiver;
    bool                 CreatesBinding;
} DMABindingRegistration_t;

/**
 * @brief Fixed copy of the DMA route, with its recovery group, event and binding state.
 * This record belongs to the manager's registry. A pending event or current
 * binding prevents removal. Changing groups requires UpdateLock and Lock.
 */
typedef struct DMAAttachment {
    element_t                   Link;
    OSDmaAttachmentParameters_t Configuration;
    uint64_t                    ID;
    uint64_t                    RecoveryGroup;
    DMAAttachmentEventState_t   Event;
    DMAAttachmentBindingState_t Binding;
} DMAAttachment_t;

/**
 * @brief Attachment and its proposed recovery group ID. Zero marks removal.
 * Removal preparation uses temporary group labels before reserving the final IDs.
 */
typedef struct DMARecoveryEntry {
    DMAAttachment_t* Attachment;
    uint64_t         RecoveryGroup;
} DMARecoveryEntry_t;

/**
 * @brief Proposed recovery group for each attachment affected by an update.
 * UpdateLock keeps the attachment pointers valid. Check every entry for pending
 * events and current bindings before applying the update under Lock.
 */
typedef struct DMARecoveryUpdate {
    uint32_t           Count;
    DMARecoveryEntry_t Entries[DMA_MANAGER_MAX_ATTACHMENTS];
} DMARecoveryUpdate_t;

/**
 * @brief Prepared attachment registration waiting for its ID to be copied to the caller.
 * UpdateLock stays held. CreatesAttachment selects a new record rather than a
 * retry, even before the record is allocated. RecoveryUpdate is NULL for a retry.
 * The caller must keep its manager operation reference until publish or cancel finishes.
 */
typedef struct DMAAttachmentRegistration {
    DMAManager_t*        Manager;
    DMAAttachment_t*     Attachment;
    DMARecoveryUpdate_t* RecoveryUpdate;
    bool                 CreatesAttachment;
} DMAAttachmentRegistration_t;

/**
 * @brief Calculate the recovery group formed by adding an attachment.
 * Requires UpdateLock and a manager operation reference. The new attachment must
 * not yet be registered. Takes Lock to read the registry and reserve IDs, and
 * allocates outside Lock. Failure leaves existing recovery groups unchanged.
 *
 * @param manager Manager being updated.
 * @param addedAttachment Validated attachment to add to the registry.
 * @param updateOut Receives the prepared group changes only on success.
 * @return OS_EOK, OS_EOOM, OS_EOVERFLOW or OS_ECANCELLED.
 */
KERNELABI oserr_t KERNELAPI
DMARecoveryPrepareRegistration(
    _In_  DMAManager_t*         manager,
    _In_  DMAAttachment_t*      addedAttachment,
    _Out_ DMARecoveryUpdate_t** updateOut);

/**
 * @brief Calculate the recovery groups left after removing an attachment.
 * Requires UpdateLock and a manager operation reference. The attachment stays
 * registered during this call. Check for pending events and current bindings
 * before applying the changes.
 *
 * @param manager Manager being updated.
 * @param removedAttachment Registered attachment whose recovery group is being rebuilt.
 * @param updateOut Receives the prepared group changes only on success.
 * @return OS_EOK, OS_EOOM, OS_EOVERFLOW or OS_ECANCELLED.
 */
KERNELABI oserr_t KERNELAPI
DMARecoveryPrepareRemoval(
    _In_  DMAManager_t*         manager,
    _In_  DMAAttachment_t*      removedAttachment,
    _Out_ DMARecoveryUpdate_t** updateOut);

/**
 * @brief Apply the prepared group IDs. Does not allocate and cannot fail.
 * Requires Lock, UpdateLock and a live manager. None of the affected attachments
 * may have pending events or current bindings.
 *
 * @param update Prepared changes whose attachment pointers remain valid.
 */
KERNELABI void KERNELAPI
DMARecoveryApplyUpdate(
    _In_ DMARecoveryUpdate_t* update);

/**
 * @brief Free a prepared group update and clear its pointer.
 * Leaves the attachments unchanged.
 *
 * @param update Prepared update to free. Does nothing if *update is NULL.
 */
KERNELABI void KERNELAPI
DMARecoveryDiscardUpdate(
    _InOut_ DMARecoveryUpdate_t** update);

/**
 * @brief Queue an attachment event or add reason flags to its pending event.
 * Requires Lock, a registered attachment and valid, nonzero reason flags.
 * If an event is already pending, generation and stopEpoch must match it.
 * Call MarkHandle after releasing Lock, while still holding the manager reference.
 * Does not allocate. Closed managers ignore events.
 *
 * @param manager Manager containing the attachment.
 * @param attachment Attachment whose state changed.
 * @param generation Binding generation for this event.
 * @param stopEpoch Recovery epoch, or zero for an idle event.
 * @param reasons DMA_ATTACHMENT_* reason flags to merge.
 */
KERNELABI void KERNELAPI
DMAAttachmentQueueEventLocked(
    _In_ DMAManager_t*    manager,
    _In_ DMAAttachment_t* attachment,
    _In_ uint64_t         generation,
    _In_ uint64_t         stopEpoch,
    _In_ uint32_t         reasons);

/**
 * @brief Mark a binding permanently closed without changing its references.
 * Requires the binding manager's Lock. Does not allocate or send notifications.
 *
 * @param binding Binding to close. It may still be waiting for publication.
 */
KERNELABI void KERNELAPI
DMABindingCloseLocked(
    _In_ DMABinding_t* binding);

#endif //!__DMA_ATTACHMENT_PRIVATE_H__

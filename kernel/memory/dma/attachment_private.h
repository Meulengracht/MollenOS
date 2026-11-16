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
#include <stdatomic.h>

DECL_STRUCT(MSContextSecurity);
DECL_STRUCT(DMABinding);
DECL_STRUCT(DMAAttachment);

enum DMAManagerState {
    DMAManagerPreparing,
    DMAManagerLive,
    DMAManagerClosed
};

enum DMARecoveryState {
    DMARecoveryLive,
    DMARecoveryBlocked,
    DMARecoveryConfirmed,
    DMARecoveryCleaned
};

/**
 * @brief Tracks whether new DMA work can start and how many operations are running.
 * Hold group->Lock.
 */
typedef struct DMARecoveryGate {
    enum DMARecoveryState State;
    unsigned int          Operations;
    bool                  ManagerClosed;
} DMARecoveryGate_t;

/**
 * @brief Which cleanup or cancel step is currently running in this stop round.
 * Hold group->Lock.
 */
enum DMARecoveryStopPhase {
    DMARecoveryStopIdle,
    DMARecoveryStopCancelling,
    DMARecoveryStopCancelAgain,
    DMARecoveryStopCleaning
};

/**
 * @brief Stop round counter, active cleanup step, and last cancellation result.
 *
 * Hold group->Lock. Both cancellation phases block cleanup until they finish.
 * If cleanup fails, Phase goes back to Idle while Gate.State stays Confirmed so
 * the manager can retry cleanup with the same ticket.
 */
typedef struct DMARecoveryStop {
    uint64_t                  Epoch;
    oserr_t                   CancellationError;
    enum DMARecoveryStopPhase Phase;
} DMARecoveryStop_t;

/**
 * @brief Group of devices that share a bus path or reset line and must stop together.
 *
 * Kept alive by internal references from the manager's group list and from
 * bindings. Because every caller already keeps the manager alive, the group
 * holds no reference back to Manager.
 *
 * Locking order: always take manager->Lock before group->Lock. Callers that
 * only check whether an operation can start take group->Lock on its own and
 * must never try to take manager->Lock while holding it. Drop both locks before
 * calling into SHM to cancel waiters or free mappings.
 */
typedef struct DMARecoveryGroup {
    element_t         Link;
    DMAManager_t*     Manager;
    uint64_t          ID;
    atomic_uint       References;
    Mutex_t           Lock;
    DMARecoveryGate_t Gate;
    DMARecoveryStop_t Stop;
} DMARecoveryGroup_t;

/**
 * @brief Registered attachments, active recovery groups, and ID counters.
 * Hold manager->Lock before reading or changing any field.
 */
typedef struct DMAAttachmentRegistry {
    list_t   Attachments;
    list_t   Groups;
    uint32_t Count;
    uint64_t NextAttachmentID;
    uint64_t NextRecoveryGroupID;
    uint64_t NextBindingGeneration;
} DMAAttachmentRegistry_t;

/**
 * @brief Top-level DMA manager handle and registry.
 *
 * Lock protects State, the registry, and waiting events. UpdateLock allows only
 * one attachment or binding change at a time, including across copying results
 * back to user space. Closing the manager skips UpdateLock so a stuck user-space
 * copy can never block handle cleanup. Saving Principal as a plain ID without
 * taking a handle reference on the service's security context lets the service
 * process exit and close this manager cleanly.
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
 * @brief Waiting event state for one attachment.
 *
 * Sequence bumps every time an event is queued or updated, and never resets
 * when read. Once it hits UINT64_MAX, the event stays queued permanently so an
 * older snapshot can never match a newer change after wraparound.
 * Hold manager->Lock to read or change any field.
 */
typedef struct DMAAttachmentEventState {
    element_t              Link;
    OSDmaAttachmentEvent_t Value;
    uint64_t               Sequence;
    bool                   Queued;
} DMAAttachmentEventState_t;

enum DMABindingState {
    DMABindingPreparing,
    DMABindingLive,
    DMABindingClosed
};

/**
 * @brief Active binding pointer and history of the last published bind request.
 *
 * Current does not keep the binding struct alive. The binding's delete callback
 * clears Current under manager->Lock while leaving Principal, RequestId, and
 * Generation in place so late retries can still tell what happened. Principal
 * stays 0 until the first binding is published. Hold manager->Lock (plus
 * UpdateLock when publishing).
 */
typedef struct DMAAttachmentBindingState {
    DMABinding_t* Current;
    uint64_t      Principal;
    uint64_t      RequestId;
    uint64_t      Generation;
} DMAAttachmentBindingState_t;

enum DMABindingContextPhase {
    DMABindingContextNone,
    DMABindingContextPreparing,
    DMABindingContextLive
};

/**
 * @brief Points to the driver's open DMA context without keeping it alive.
 * Hold group->Lock.
 */
typedef struct DMABindingContextState {
    struct SHMDeviceContext*    Pointer;
    enum DMABindingContextPhase Phase;
} DMABindingContextState_t;

/**
 * @brief Driver binding handle and its link back to the manager and attachment.
 *
 * Attachment stays NULL until DMAManagerPublishBinding runs. Group is acquired
 * before creating the handle so even an unpublished binding can lock group->Lock.
 * The context holds a reference to this binding, while the binding holds no
 * reference back to the context.
 */
typedef struct DMABinding {
    uuid_t               ID;
    DMAManager_t*        Manager;
    DMAAttachment_t*     Attachment;
    uint64_t             Principal;
    uint64_t             RequestId;
    uint64_t             Generation;
    enum DMABindingState State;
    DMARecoveryGroup_t*  Group;
    DMABindingContextState_t Context;
} DMABinding_t;

/**
 * @brief Prepared binding update waiting to be published or cancelled.
 *
 * UpdateLock stays held while this exists. Receiver keeps the driver's security
 * context alive until finished, and Binding holds a handle reference.
 * CreatesBinding is true for a new binding and false for an identical retry.
 * Keep the caller's manager reference alive until publish or cancel finishes.
 */
typedef struct DMABindingRegistration {
    DMAManager_t*        Manager;
    DMAAttachment_t*     Attachment;
    DMABinding_t*        Binding;
    MSContextSecurity_t* Receiver;
    bool                 CreatesBinding;
} DMABindingRegistration_t;

/**
 * @brief Registered device DMA setup, recovery group, event, and binding state.
 *
 * Lives in the manager's registry and cannot be removed while an event is
 * queued or a binding still exists. Reassigning its group requires both
 * UpdateLock and Lock.
 */
typedef struct DMAAttachment {
    element_t                   Link;
    OSDmaAttachmentParameters_t Configuration;
    uint64_t                    ID;
    DMARecoveryGroup_t*         RecoveryGroup;
    DMAAttachmentEventState_t   Event;
    DMAAttachmentBindingState_t Binding;
} DMAAttachment_t;

/**
 * @brief Pairs an attachment with its proposed new recovery group ID (0 means removed).
 * Removal setup stores temporary group numbers here before reserving final IDs.
 */
typedef struct DMARecoveryEntry {
    DMAAttachment_t* Attachment;
    uint64_t         RecoveryGroup;
} DMARecoveryEntry_t;

/**
 * @brief Prepared recovery group changes for adding or removing an attachment.
 *
 * UpdateLock keeps all Attachment pointers valid until applied or freed.
 * Before applying under manager->Lock, verify that none of the listed
 * attachments have waiting events or active bindings.
 */
typedef struct DMARecoveryUpdate {
    DMAManager_t*      Manager;
    uint32_t           Count;
    DMARecoveryEntry_t Entries[DMA_MANAGER_MAX_ATTACHMENTS];
    uint32_t           NewCount;
    DMARecoveryGroup_t* NewGroups[DMA_MANAGER_MAX_ATTACHMENTS];
    uint32_t           OldCount;
    DMARecoveryGroup_t* OldGroups[DMA_MANAGER_MAX_ATTACHMENTS];
} DMARecoveryUpdate_t;

/**
 * @brief Prepared attachment registration waiting for its ID to copy to user space.
 *
 * UpdateLock stays held while this exists. CreatesAttachment is true for a new
 * entry and false for an identical retry, even before an attachment struct is
 * allocated. RecoveryUpdate is NULL for a retry. Keep the caller's manager
 * reference alive until publish or cancel finishes.
 */
typedef struct DMAAttachmentRegistration {
    DMAManager_t*        Manager;
    DMAAttachment_t*     Attachment;
    DMARecoveryUpdate_t* RecoveryUpdate;
    bool                 CreatesAttachment;
} DMAAttachmentRegistration_t;

/**
 * @brief Computes the merged recovery group for adding a new attachment.
 *
 * Call with UpdateLock and a manager reference held, before adding addedAttachment
 * to the registry. Briefly takes manager->Lock to read existing groups and
 * reserve an ID, then allocates memory outside the lock. Failure leaves existing
 * recovery groups untouched.
 *
 * @param manager         Manager being updated.
 * @param addedAttachment Validated attachment about to be registered.
 * @param updateOut       Receives the prepared group changes on success.
 * @return OS_EOK, OS_EOOM, OS_EOVERFLOW, or OS_ECANCELLED.
 */
KERNELABI oserr_t KERNELAPI
DMARecoveryPrepareRegistration(
    _In_  DMAManager_t*         manager,
    _In_  DMAAttachment_t*      addedAttachment,
    _Out_ DMARecoveryUpdate_t** updateOut);

/**
 * @brief Computes the new recovery groups left behind when removing an attachment.
 *
 * Call with UpdateLock and a manager reference held while removedAttachment is
 * still in the registry. Check for pending events and bindings before applying.
 *
 * @param manager           Manager being updated.
 * @param removedAttachment Attachment about to be removed.
 * @param updateOut         Receives the prepared group changes on success.
 * @return OS_EOK, OS_EOOM, OS_EOVERFLOW, or OS_ECANCELLED.
 */
KERNELABI oserr_t KERNELAPI
DMARecoveryPrepareRemoval(
    _In_  DMAManager_t*         manager,
    _In_  DMAAttachment_t*      removedAttachment,
    _Out_ DMARecoveryUpdate_t** updateOut);

/**
 * @brief Swaps the prepared recovery groups into place without allocating or failing.
 *
 * Call under manager->Lock and UpdateLock while the manager is live. None of
 * the affected attachments may have pending events or active bindings.
 *
 * @param update Prepared changes whose attachment pointers are still valid.
 */
KERNELABI void KERNELAPI
DMARecoveryApplyUpdate(
    _In_ DMARecoveryUpdate_t* update);

/**
 * @brief Frees a prepared group update without changing any attachment.
 *
 * @param update Update pointer to free and set to NULL. No-op if *update is NULL.
 */
KERNELABI void KERNELAPI
DMARecoveryDiscardUpdate(
    _InOut_ DMARecoveryUpdate_t** update);

/**
 * @brief Adds an event for an attachment or merges flags into its queued event.
 *
 * Hold manager->Lock. The attachment must be registered and reasons nonzero.
 * Merging into an already waiting event requires the same generation and stop
 * round, except DMA_ATTACHMENT_STOP_REQUIRED can upgrade a clean idle event
 * from round 0 to the current stop round (bumping Sequence so an older read
 * cannot clear the upgraded event).
 *
 * Never allocates memory, and does nothing if the manager already closed. Call
 * DMAManagerNotifyEvents after dropping Lock while holding a manager reference.
 *
 * @param manager    Manager that holds the attachment.
 * @param attachment Attachment whose state changed.
 * @param generation Binding generation for the event.
 * @param stopEpoch  Stop round number, or 0 for a clean idle event.
 * @param reasons    Bitmask of DMA_ATTACHMENT_* flags to set.
 */
KERNELABI void KERNELAPI
DMAAttachmentQueueEventLocked(
    _In_ DMAManager_t*    manager,
    _In_ DMAAttachment_t* attachment,
    _In_ uint64_t         generation,
    _In_ uint64_t         stopEpoch,
    _In_ uint32_t         reasons);

/**
 * @brief Wakes watchers waiting on the manager handle because events are queued.
 *
 * Call without manager->Lock while holding a manager reference. If waking
 * watchers fails, the events stay queued for the next read.
 *
 * @param manager Manager with waiting events.
 * @param caller  Caller function name for error logs.
 */
KERNELABI void KERNELAPI
DMAManagerNotifyEvents(
    _In_ DMAManager_t* manager,
    _In_ const char*   caller);

/**
 * @brief Marks a binding closed without dropping any of its references.
 *
 * Hold the binding manager's Lock. Does not allocate memory or wake handle watchers.
 *
 * @param binding Binding to close (may still be waiting to be published).
 */
KERNELABI void KERNELAPI
DMABindingCloseLocked(
    _In_ DMABinding_t* binding);

/**
 * @brief Increments a recovery group's internal reference count if it is nonzero.
 *
 * Requires an existing reference or manager->Lock protecting the group in the registry.
 */
KERNELABI oserr_t KERNELAPI
DMARecoveryGroupAcquire(
    _In_ DMARecoveryGroup_t* group);

/**
 * @brief Drops a group reference and frees the group when the last one is gone.
 *
 * Call outside manager->Lock and group->Lock. Before the final reference drops,
 * the group must be removed from the registry with zero active operations.
 */
KERNELABI void KERNELAPI
DMARecoveryGroupRelease(
    _InOut_ DMARecoveryGroup_t** group);

/**
 * @brief Blocks the whole recovery group and closes its bindings under manager->Lock.
 *
 * Takes group->Lock internally and queues STOP_REQUIRED events without
 * allocating memory. Leaves SHM cancellation for DMARecoveryCancel outside locks.
 */
KERNELABI void KERNELAPI
DMARecoveryBlockLocked(
    _In_ DMAManager_t*       manager,
    _In_ DMARecoveryGroup_t* group);

/**
 * @brief Cancels all SHM contexts in a blocked recovery group.
 *
 * Call outside manager->Lock and group->Lock with a valid group reference.
 * Only one cancel pass runs at a time. If another request arrives mid-pass,
 * the loop runs again. A failed pass keeps CompleteStop from confirming cleanup.
 */
KERNELABI void KERNELAPI
DMARecoveryCancel(
    _In_ DMARecoveryGroup_t* group);

#endif //!__DMA_ATTACHMENT_PRIVATE_H__

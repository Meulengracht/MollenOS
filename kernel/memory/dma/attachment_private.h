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
 * Internal DMA manager structures and helpers. Covers the manager registry,
 * recovery groups, driver bindings, event queues, and translated domain
 * connections shared across files in kernel/memory/dma/.
 */

#ifndef __DMA_ATTACHMENT_PRIVATE_H__
#define __DMA_ATTACHMENT_PRIVATE_H__

#include <ds/list.h>
#include <mutex.h>
#include <spinlock.h>
#include <stdatomic.h>

#include "domain.h"

DECL_STRUCT(MSContextSecurity);
DECL_STRUCT(DMABinding);
DECL_STRUCT(DMAAttachment);

// Lifecycle states for a DMA manager handle.
// Preparing: allocated and registered globally, waiting for DMAManagerPublish.
// Live:      open and accepting attachment, domain, and binding requests.
// Closed:    shutting down or closed. Refuses new requests and stops all groups.
enum DMAManagerState {
    DMAManagerPreparing,
    DMAManagerLive,
    DMAManagerClosed
};

// Gate states controlling whether drivers in a recovery group can start DMA.
// Live:          normal operation. New DMA operations can begin.
// Blocked:       stop requested. New operations are refused while running ones drain.
// StopConfirmed: all drivers in the group stopped and cancelled their contexts.
// StopCleaned:   shared-memory cleanup ran after the stop. Ready to become Live again.
enum DMARecoveryState {
    DMARecoveryLive,
    DMARecoveryBlocked,
    DMARecoveryStopConfirmed,
    DMARecoveryStopCleaned
};

/**
 * @brief Tracks whether new DMA work can start and how many operations are running.
 * Lock protects the state, the operation count and cleanup exclusion. IRQs stay
 * disabled only during these short updates. Slow paths may read the atomic
 * fields under group->Lock. They must also take Lock before changing them.
 */
typedef struct DMARecoveryOperationState {
    Spinlock_t                    Lock;
    _Atomic(enum DMARecoveryState) State;
    atomic_uint                   Operations;
    atomic_bool                   ManagerClosed;
    bool                          Cleaning;
} DMARecoveryOperationState_t;

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
 * If cleanup fails, Phase goes back to Idle while Access.State stays Confirmed so
 * the manager can retry cleanup with the same ticket.
 */
typedef struct DMARecoveryStop {
    uint64_t                  Round;
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
 * only check or count operations take Access.Lock. When nested, Access.Lock
 * comes after these mutexes. Never take a mutex, call SHM cleanup, log, or scan
 * attachments with the spinlock held. Drop the mutexes before cancelling waiters
 * or freeing mappings.
 */
typedef struct DMARecoveryGroup {
    element_t                   Link;
    DMAManager_t*               Manager;
    uint64_t                    ID;
    atomic_uint                 References;
    Mutex_t                     Lock;
    DMARecoveryOperationState_t Access;
    DMARecoveryStop_t           Stop;
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

// Lifecycle states for a translated domain.
// Ready:   accepting attachments and bindings (once its connection is Connected).
// Closing: destruction started. Keeps its IDs reserved until cleanup succeeds.
enum DMATranslatedDomainState {
    DMATranslatedDomainReady,
    DMATranslatedDomainClosing
};

// Hardware routing state for a domain's reserved hardware IDs.
// Prepared:      IDs reserved in software. No hardware routes changed yet.
// Connecting:    ConnectHardwareIDs is running (or failed partway through).
// Connected:     every hardware ID points to the domain's address space.
// Disconnecting: DisconnectHardwareIDs is running (or failed partway through).
// Disconnected:  hardware routes are cleared. Ready for software-only cleanup.
enum DMAConnectionState {
    DMAConnectionPrepared,
    DMAConnectionConnecting,
    DMAConnectionConnected,
    DMAConnectionDisconnecting,
    DMAConnectionDisconnected
};

/**
 * @brief A registered domain: devices and hardware IDs that share one IOMMU address space.
 *
 * Manager->Lock protects State, Members, and OwnerSecurityID. Hold UpdateLock to
 * change membership or destroy the domain, so only one membership change runs
 * at a time. The HardwareIDs and Devices arrays, AddressLimit, and CachePolicy
 * never change after creation. Attachments keep the domain alive through
 * Members and their binding rules.
 *
 * A Closing domain has zero member attachments. Its device IDs, hardware IDs,
 * and RequesterGroup stay reserved while DMATranslatedDomainDestroy tears down
 * the connection and address space outside manager locks. Recovery code must
 * skip any Closing domain before reading AddressSpace, because the space may
 * already be freed.
 *
 * The domain holds one manager reference and the creator's reference to
 * AddressSpace. ConnectionSpace holds a second address-space reference until
 * hardware ID cleanup finishes, so page tables stay allocated while hardware
 * may still use them. While Ready, manager->Lock protects ConnectionState.
 * Before registration and after Closing, only the single-threaded create or
 * destroy caller touches connection fields.
 *
 * FaultCount is the number of device faults DMAManagerReportFault has counted
 * for this domain. It starts at zero, stops at UINT64_MAX and is never reset.
 * It decides which faults are logged and sent to the device manager. Hold
 * manager->Lock to read or change it.
 */
struct DMATranslatedDomain {
    element_t                     Link;
    DMAManager_t*                 Manager;
    DMAIOMMUUnit_t*               Unit;
    struct DMAAddressSpace*       AddressSpace;
    enum DMATranslatedDomainState State;
    enum DMAConnectionState       ConnectionState;
    void*                        Connection;
    struct DMAAddressSpace*       ConnectionSpace;
    uuid_t                        Host;
    uint64_t                      RequesterGroup;
    enum SHMDeviceCachePolicy     CachePolicy;
    uint64_t                      AddressLimit;
    uint64_t                      OwnerSecurityID;
    uint32_t                      Members;
    uint32_t                      HardwareIDCount;
    uint32_t*                     HardwareIDs;
    uint32_t                      DeviceCount;
    uuid_t*                       Devices;
    uint64_t                      FaultCount;
};

/**
 * @brief Reserves the domain's full hardware ID set before registering the domain.
 *
 * Call without manager or group locks held. Takes an extra reference on the
 * address space before calling the backend. Even if the IOMMU unit fails during
 * preparation, the backend may still return a prepared connection. Always call
 * DMAConnectionDestroy before discarding the domain.
 *
 * @param domain Newly created domain with a valid AddressSpace and HardwareIDs.
 * @return OS_EOK on success, or an address-space or backend error.
 */
KERNELABI oserr_t KERNELAPI
DMAConnectionPrepare(
    _In_ DMATranslatedDomain_t* domain);

/**
 * @brief Disconnects hardware IDs, frees connection tracking, and drops ConnectionSpace.
 *
 * The domain must be unregistered or Closing, with no concurrent create or
 * destroy calls. Call without manager or group locks held. A Prepared
 * connection has touched no hardware routes yet, so software cleanup alone runs
 * even on a failed unit. Other states require disconnecting the hardware IDs
 * while the unit stays healthy.
 *
 * On error, keep the connection, hardware ID reservations, and ConnectionSpace
 * reference intact so the caller can retry later. Returns OS_EOK immediately
 * when Connection is NULL.
 *
 * @param domain Valid domain whose hardware ID connection should be torn down.
 * @return OS_EOK on success, or an error if disconnecting fails or the unit failed.
 */
KERNELABI oserr_t KERNELAPI
DMAConnectionDestroy(
    _In_ DMATranslatedDomain_t* domain);

/**
 * @brief Top-level DMA manager handle and registry.
 *
 * Lock protects State, the registry, and waiting events. UpdateLock allows only
 * one attachment or binding change at a time, including across copying results
 * back to user space. Closing the manager skips UpdateLock so a stuck user-space
 * copy can never block handle cleanup. Saving OwnerSecurityID as a plain ID
 * without taking a handle reference on the service's security context lets the
 * service process exit and close this manager cleanly. CheckedFailureSequence
 * records the last completed IOMMU fault scan and can be read without Lock.
 */
typedef struct DMAManager {
    uuid_t                  ID;
    uint64_t                OwnerSecurityID;
    enum DMAManagerState    State;
    Mutex_t                 Lock;
    Mutex_t                 UpdateLock;
    DMAAttachmentRegistry_t Registry;
    list_t                  PendingEvents;
    list_t                  Domains;
    uint32_t                DomainCount;
    atomic_uint             CheckedFailureSequence;
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

// Lifecycle states for a driver's binding handle to an attachment.
enum DMABindingState {
    DMABindingPreparing,
    DMABindingLive,
    DMABindingClosed
};

/**
 * @brief Active binding pointer and history of the last published bind request.
 *
 * Current does not keep the binding struct alive. The binding's delete callback
 * clears Current under manager->Lock while leaving OwnerSecurityID,
 * RequestId, and Generation in place so late retries can still tell what
 * happened. OwnerSecurityID stays 0 until the first binding is published. Hold
 * manager->Lock (plus UpdateLock when publishing).
 */
typedef struct DMAAttachmentBindingState {
    DMABinding_t* Current;
    uint64_t      OwnerSecurityID;
    uint64_t      RequestId;
    uint64_t      Generation;
} DMAAttachmentBindingState_t;

// Tracks whether a shared-memory DMA context is attached to a binding.
enum DMABindingContextPhase {
    DMABindingContextNone,
    DMABindingContextPreparing,
    DMABindingContextLive
};

/**
 * @brief Points to the driver's open DMA context without keeping it alive.
 * Pointer requires group->Lock. Phase changes also take Access.Lock so fast
 * operation start can read it without taking the mutex.
 */
typedef struct DMABindingContextState {
    struct SHMDeviceContext*            Pointer;
    _Atomic(enum DMABindingContextPhase) Phase;
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
    uint64_t             OwnerSecurityID;
    uint64_t             RequestId;
    uint64_t             Generation;
    _Atomic(enum DMABindingState) State;
    DMARecoveryGroup_t*  Group;
    DMABindingContextState_t Context;
} DMABinding_t;

/**
 * @brief Prepared binding update waiting to be published or cancelled.
 *
 * UpdateLock stays held while this exists. DriverSecurity keeps the driver's
 * security context alive until finished, and Binding holds a handle reference.
 * CreatesBinding is true for a new binding and false for an identical retry.
 * Keep the caller's manager reference alive until publish or cancel finishes.
 */
typedef struct DMABindingRegistration {
    DMAManager_t*        Manager;
    DMAAttachment_t*     Attachment;
    DMABinding_t*        Binding;
    MSContextSecurity_t* DriverSecurity;
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
    DMATranslatedDomain_t*      Domain; // NULL for direct attachments.
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
 * cannot clear the upgraded event). DMA_ATTACHMENT_DEVICE_FAULT is queued on
 * its own and never moves an event to another stop round.
 *
 * Never allocates memory, and does nothing if the manager already closed. Call
 * DMAManagerNotifyEvents after dropping Lock while holding a manager reference.
 *
 * @param manager    Manager that holds the attachment.
 * @param attachment Attachment whose state changed.
 * @param generation Binding generation for the event.
 * @param stopRound  Stop round number, or 0 for a clean idle event.
 * @param reasons    Bitmask of DMA_ATTACHMENT_* flags to set.
 */
KERNELABI void KERNELAPI
DMAAttachmentQueueEventLocked(
    _In_ DMAManager_t*    manager,
    _In_ DMAAttachment_t* attachment,
    _In_ uint64_t         generation,
    _In_ uint64_t         stopRound,
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
 *
 * @param group Recovery group to reference.
 * @return OS_EOK on success, or an error if the group has no remaining references.
 */
KERNELABI oserr_t KERNELAPI
DMARecoveryGroupAcquire(
    _In_ DMARecoveryGroup_t* group);

/**
 * @brief Drops a group reference and frees the group when the last one is gone.
 *
 * Call outside manager->Lock and group->Lock. Before the final reference drops,
 * the group must be removed from the registry with zero active operations.
 *
 * @param group Address of the group pointer. Cleared to NULL.
 */
KERNELABI void KERNELAPI
DMARecoveryGroupRelease(
    _InOut_ DMARecoveryGroup_t** group);

/**
 * @brief Blocks the whole recovery group and closes its bindings under manager->Lock.
 *
 * Takes group->Lock internally and queues STOP_REQUIRED events without
 * allocating memory. Leaves shared-memory (SHM) cancellation for
 * DMARecoveryCancel outside locks.
 *
 * @param manager Manager holding the group.
 * @param group   Recovery group to block.
 */
KERNELABI void KERNELAPI
DMARecoveryBlockLocked(
    _In_ DMAManager_t*       manager,
    _In_ DMARecoveryGroup_t* group);

/**
 * @brief Cancels all shared-memory (SHM) contexts in a blocked recovery group.
 *
 * Call outside manager->Lock and group->Lock with a valid group reference.
 * Only one cancel pass runs at a time. If another request arrives mid-pass,
 * the loop runs again. A failed pass keeps CompleteStop from confirming cleanup.
 *
 * @param group Blocked recovery group whose contexts should cancel.
 */
KERNELABI void KERNELAPI
DMARecoveryCancel(
    _In_ DMARecoveryGroup_t* group);

/**
 * @brief Drops the global registry's reference to an empty closed manager.
 *
 * Call without manager->Lock or UpdateLock held, and while holding another
 * manager reference. Does nothing if the manager is still open. Registered
 * attachments and domains also keep the global entry in place.
 *
 * @param manager Closed manager to check and detach.
 */
KERNELABI void KERNELAPI
DMAManagerDetachClosed(
    _In_ DMAManager_t* manager);

/**
 * @brief Block recovery groups on failed Ready domains, then cancel them outside the registry locks.
 *
 * Hold a manager reference throughout the call. No backend, manager, group or
 * SHM lock may be held. Every affected group is blocked and has its events
 * queued before any step that can fail begins. If an error occurs, the gates
 * and events stay in place so a later call can retry. Closing domains are
 * skipped, because their address spaces may already be freed.
 *
 * @param manager Manager whose domains are checked.
 * @return OS_EOK, or the first error from taking a group reference or from an
 *         incomplete cancellation.
 */
KERNELABI oserr_t KERNELAPI
DMARecoveryBlockFailedUnits(
    _In_ DMAManager_t* manager);

/**
 * @brief Propagate a newly reported IOMMU failure before admitting or ending work.
 * The unchanged sequence path only reads atomics. A changed sequence runs the
 * full failure pass without holding Access.Lock. Slow recovery callers use
 * DMARecoveryBlockFailedUnits directly to retry cancellation even without a new fault.
 */
KERNELABI oserr_t KERNELAPI
DMARecoveryCheckFailures(
    _In_ DMAManager_t* manager);

/**
 * @brief Counts one more running operation in a recovery group.
 * Call with access->Lock held. The count is changed with a plain store so that
 * readers without the lock never see a torn value.
 *
 * @param access Operation state of the recovery group.
 * @return OS_EOK, or OS_EOVERFLOW if the count has no room left.
 */
KERNELABI oserr_t KERNELAPI
DMARecoveryAddOperationLocked(
    _In_ DMARecoveryOperationState_t* access);

/**
 * @brief Removes one running operation from a recovery group.
 * Call with access->Lock held. The count must not be zero.
 *
 * @param access Operation state of the recovery group.
 */
KERNELABI void KERNELAPI
DMARecoveryRemoveOperationLocked(
    _In_ DMARecoveryOperationState_t* access);

/** Check a referenced binding while group->Lock or Access.Lock is held. */
KERNELABI oserr_t KERNELAPI
DMABindingCheckLiveLocked(
    _In_ DMABinding_t* binding);

#endif //!__DMA_ATTACHMENT_PRIVATE_H__

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
 * DMA manager, attachment, binding, and recovery-group interface. Handles
 * registering device attachments, handing driver binding handles to drivers,
 * tracking active DMA operations, and stopping devices cleanly on teardown or
 * hardware failure.
 */

#ifndef __DMA_ATTACHMENT_H__
#define __DMA_ATTACHMENT_H__

#include <os/types/dma_attachment.h>

// Forward declarations for opaque types
DECL_STRUCT(DMAManager);
DECL_STRUCT(DMABinding);
DECL_STRUCT(SHMDeviceContext);
DECL_STRUCT(DMAAttachmentRegistration);
DECL_STRUCT(DMABindingRegistration);

/**
 * @brief Copied batch of waiting attachment events and their sequence numbers.
 *
 * Holds copies of the event structs so the caller can work with them after
 * dropping the manager lock. Keep the batch unchanged and keep a manager
 * reference alive across the snapshot, user-space copies, and the
 * DMAManagerFinishEvents call. Entries and Sequences line up by index from 0
 * to Count - 1.
 */
typedef struct DMAAttachmentEventBatch {
    uint32_t               Count;
    OSDmaAttachmentEvent_t Entries[DMA_MANAGER_MAX_EVENTS];
    uint64_t               Sequences[DMA_MANAGER_MAX_EVENTS];
} DMAAttachmentEventBatch_t;

/**
 * @brief Allocates the DMA manager and creates its handle before enabling it.
 *
 * Requires an active security context (the process group owning the handle)
 * and CAPABILITY_DEVICEIO_MANAGEMENT. Creation happens in two steps (prepare,
 * then publish) so the handle can be copied to user space before the manager
 * begins accepting commands. Both the global registry and managerOut hold a
 * handle reference, alongside the handle installed in the caller's handle
 * table, keeping the manager alive while the caller uses it and while any
 * attachments remain registered.
 *
 * @param managerOut Receives a handle reference for this system call. Call
 *                   DMAManagerRelease after publishing or cancelling.
 * @param handleOut  Receives the new handle ID to copy back to user space.
 * @return OS_EOK, OS_EBUSY if a manager already exists, or a security or
 *         memory error. Failure leaves both outputs unchanged.
 */
KERNELABI oserr_t KERNELAPI
DMAManagerPrepareCreate(
    _Out_ DMAManager_t** managerOut,
    _Out_ uuid_t*        handleOut);

/**
 * @brief Enables a prepared manager once its handle has been copied to user space.
 *
 * @param manager Prepared manager kept alive by DMAManagerPrepareCreate.
 * @return OS_EOK, or OS_ECANCELLED if the handle or caller's security group
 *         closed in the meantime. On failure, call DMAManagerCancelCreate
 *         before calling DMAManagerRelease.
 */
KERNELABI oserr_t KERNELAPI
DMAManagerPublish(
    _In_ DMAManager_t* manager);

/**
 * @brief Closes a manager whose setup failed and removes its user handle.
 *
 * Call this only when creating the manager or copying its handle failed. Marks
 * the manager closed right away even if temporary handle references delay final
 * freeing. You still need to drop the caller's own handle reference with
 * DMAManagerRelease.
 *
 * @param manager Prepared manager, including one that failed during publish.
 */
void
DMAManagerCancelCreate(
    _In_ DMAManager_t* manager);

/**
 * @brief Looks up a live manager and checks the caller's handle and security group.
 *
 * @param handle     Manager handle passed from user space.
 * @param managerOut Receives a reference that keeps the manager alive during the
 *                   call. Unchanged on failure.
 * @return Handle errors, OS_EPERMISSIONS if the caller is in a different security
 *         group, OS_EBUSY if still being created, or OS_ECANCELLED if closed.
 */
KERNELABI oserr_t KERNELAPI
DMAManagerReference(
    _In_  uuid_t         handle,
    _Out_ DMAManager_t** managerOut);

/**
 * @brief Drops the caller's manager reference and clears *manager to NULL.
 *
 * Call this only after releasing all manager locks and finishing or cancelling
 * any pending attachment or binding update. Safe to call after the manager closes.
 *
 * @param manager Reference pointer to release. Does nothing if *manager is NULL.
 */
KERNELABI void KERNELAPI
DMAManagerRelease(
    _InOut_ DMAManager_t** manager);

/**
 * @brief Checks an attachment setup and prepares a new entry or matching retry.
 *
 * Keep the caller's manager reference alive until publish or cancel finishes,
 * and keep parameters unchanged in kernel memory until this returns. On success,
 * UpdateLock stays held until DMAManagerPublishAttachment or
 * DMAManagerCancelAttachment runs, so the same thread must finish this update
 * before starting another.
 *
 * @param manager         Live manager from DMAManagerReference.
 * @param parameters      Fixed-size device description (unused entries must be 0).
 * @param registrationOut Receives the prepared update handle on success.
 * @param attachmentOut   Receives the new or existing attachment ID on success.
 * @return Validation or memory errors, OS_EBUSY if a conflicting setup or active
 *         work blocks the change, OS_ENOTSUPPORTED if the manager is full,
 *         OS_EOVERFLOW if IDs or event counters ran out, or OS_ECANCELLED if
 *         the manager closed. Temporary allocations are freed on failure.
 */
KERNELABI oserr_t KERNELAPI
DMAManagerPrepareAttachment(
    _In_  DMAManager_t*                      manager,
    _In_  const OSDmaAttachmentParameters_t* parameters,
    _Out_ DMAAttachmentRegistration_t**      registrationOut,
    _Out_ OSDmaAttachmentId_t*               attachmentOut);

/**
 * @brief Commits a prepared attachment and its recovery group changes.
 *
 * Call after copying the attachment ID to user space succeeds.
 *
 * @param registration Prepared update from DMAManagerPrepareAttachment. On
 *                     success, frees it, sets *registration to NULL, and unlocks UpdateLock.
 * @return OS_EOK, or an error if state changed while copying. On failure, call
 *         DMAManagerCancelAttachment to undo the update and unlock UpdateLock.
 */
KERNELABI oserr_t KERNELAPI
DMAManagerPublishAttachment(
    _InOut_ DMAAttachmentRegistration_t** registration);

/**
 * @brief Cancels a prepared attachment update and unlocks UpdateLock.
 *
 * Keep the manager reference held until this returns. Cancelling a retry of an
 * already registered attachment leaves the existing attachment intact.
 *
 * @param registration Prepared update to discard, cleared on return. No-op if NULL.
 */
KERNELABI void KERNELAPI
DMAManagerCancelAttachment(
    _InOut_ DMAAttachmentRegistration_t** registration);

/**
 * @brief Removes an attachment and rebuilds recovery groups for remaining devices.
 *
 * @param manager      Live manager referenced by the caller.
 * @param attachmentID Nonzero attachment ID within this manager.
 * @return OS_EOK, OS_ENOENT if missing, OS_EINVALPARAMS for 0, OS_EBUSY if
 *         the affected recovery group has waiting events or active bindings
 *         (including closed bindings whose handle references have not dropped
 *         yet), OS_EOVERFLOW, OS_EOOM, or OS_ECANCELLED.
 */
KERNELABI oserr_t KERNELAPI
DMAManagerRemoveAttachment(
    _In_ DMAManager_t* manager,
    _In_ uint64_t      attachmentID);

/**
 * @brief Copies up to DMA_MANAGER_MAX_EVENTS waiting events without removing them.
 *
 * Reading events in two steps lets the system call copy them to user space
 * without holding the manager lock, keeping events queued if copying fails.
 *
 * @param manager  Live manager referenced by the caller.
 * @param capacity Space available in the caller's array (must be > 0, capped at
 *                 DMA_MANAGER_MAX_EVENTS).
 * @param batchOut Receives the copied events and sequence numbers (may be 0 events).
 * @return OS_EOK, OS_EINVALPARAMS if capacity is 0, or OS_ECANCELLED if closed.
 *         If this fails while events are still waiting, it signals the handle again.
 */
KERNELABI oserr_t KERNELAPI
DMAManagerSnapshotEvents(
    _In_  DMAManager_t*              manager,
    _In_  uint32_t                   capacity,
    _Out_ DMAAttachmentEventBatch_t* batchOut);

/**
 * @brief Removes acknowledged events after copying them to user space.
 *
 * Always call this after a successful DMAManagerSnapshotEvents, even if copying
 * to user space failed. An event is removed only when outputsCopied is true, its
 * sequence number still matches what was copied (so newer updates are never
 * lost), and the sequence counter has not hit its limit. Signals the manager
 * handle again if any events remain queued.
 *
 * @param manager       Same referenced manager passed to DMAManagerSnapshotEvents.
 * @param batch         Unmodified batch returned by DMAManagerSnapshotEvents.
 * @param outputsCopied True only if both the event array and count copied to user space.
 * @return OS_EOK, or OS_ECANCELLED if the manager closed.
 */
KERNELABI oserr_t KERNELAPI
DMAManagerFinishEvents(
    _In_ DMAManager_t*                    manager,
    _In_ const DMAAttachmentEventBatch_t* batch,
    _In_ bool                             outputsCopied);

/**
 * @brief Prepares a new binding handle for a driver or looks up an active retry.
 *
 * Keeps parameters unchanged until return, and holds UpdateLock until publish
 * or cancel finishes so the same thread must complete this update before
 * starting another.
 *
 * @param manager         Live manager referenced by the service caller.
 * @param parameters      Copied bind request with attachment ID, driver group ID, and RequestId.
 * @param registrationOut Receives the prepared binding update on success.
 * @param bindingOut      Receives the driver's binding handle and generation on success.
 * @return OS_EOK, or a validation, permission, state, or memory error. Failure
 *         leaves outputs unchanged and cleans up temporary allocations.
 */
KERNELABI oserr_t KERNELAPI
DMAManagerPrepareBinding(
    _In_  DMAManager_t*                manager,
    _In_  const OSDmaBindParameters_t* parameters,
    _Out_ DMABindingRegistration_t**   registrationOut,
    _Out_ OSDmaBindingResult_t*        bindingOut);

/**
 * @brief Commits a prepared binding after copying its result back to the service.
 *
 * Checks again that neither the manager nor the binding closed while copying.
 *
 * @param registration Prepared binding from DMAManagerPrepareBinding. On success,
 *                     frees it, clears *registration, and releases UpdateLock.
 * @return OS_EOK, or OS_ECANCELLED if closed. On failure, call DMAManagerCancelBinding.
 */
KERNELABI oserr_t KERNELAPI
DMAManagerPublishBinding(
    _InOut_ DMABindingRegistration_t** registration);

/**
 * @brief Cancels a prepared binding and unlocks UpdateLock.
 *
 * For a new binding, closes the handle installed in the driver's handle table.
 * Cancelling an identical retry leaves the existing active binding untouched.
 * Unlocks UpdateLock before dropping references so handle cleanup cannot
 * deadlock on manager locks.
 *
 * @param registration Prepared binding to cancel, cleared on return. No-op if NULL.
 */
KERNELABI void KERNELAPI
DMAManagerCancelBinding(
    _InOut_ DMABindingRegistration_t** registration);

/**
 * @brief Settings for a new device context, plus the address space it will use.
 *
 * Holds the cache policy, the highest device address and one referenced
 * address space. They do not change afterwards. A direct binding creates its
 * own private space. A domain binding adds a reference to the domain's shared
 * space. The context drops its reference to AddressSpace after its last
 * mapping is gone.
 */
typedef struct DMABindingContextConfig {
    enum SHMDeviceCachePolicy CachePolicy;
    uint64_t                  AddressLimit;
    struct DMAAddressSpace*   AddressSpace;
} DMABindingContextConfig_t;

/**
 * @brief Attaches a new bound DMA context to a driver's binding handle.
 *
 * Verifies that the caller holds the binding handle, belongs to its security
 * group, and passed the exact generation. On success, it takes a binding
 * reference and counts this open as a running operation, so a stop pass waits
 * for it to finish. It also gives the caller a reference to the address space
 * in configurationOut. AddressLimit is the lower of the attachment's limit and
 * the limit the caller asked for. On failure the outputs are unchanged and the
 * counted open is undone. The address space is set up while no manager, group
 * or UpdateLock lock is held. The caller must initialize context before this
 * call, because cancellation may look at it while the open is running.
 */
KERNELABI oserr_t KERNELAPI
DMABindingAttachContext(
    _In_  uuid_t                            handle,
    _In_  const OSDmaBoundOpenParameters_t* parameters,
    _In_  SHMDeviceContext_t*               context,
    _Out_ DMABinding_t**                    bindingOut,
    _Out_ DMABindingContextConfig_t*        configurationOut);

/**
 * @brief Finishes opening a bound context once its handle is copied to user space.
 *
 * Rechecks that the binding is still live and lowers the active operation count
 * added by DMABindingAttachContext.
 */
KERNELABI oserr_t KERNELAPI
DMABindingPublishContext(
    _In_ DMABinding_t* binding);

/**
 * @brief Detaches a context from its binding and drops the context's binding reference.
 *
 * Call only when freeing the context (after all mappings are gone) or when
 * cancelling an open that failed. If the context never finished publishing,
 * this also clears the setup operation count. Drops references outside all
 * manager and group locks.
 */
KERNELABI void KERNELAPI
DMABindingDetachContext(
    _InOut_ DMABinding_t**      binding,
    _In_    SHMDeviceContext_t* context);

/**
 * @brief Checks permission and counts a new DMA operation on a bound context.
 *
 * Normal operations (creating mappings, reading segments, or starting transfers)
 * require the recovery group to be live. Release operations (unmapping existing
 * mappings) are also allowed after hardware stop is confirmed, provided no
 * stop-cleanup pass is currently running. Returns OS_EOVERFLOW if the counter
 * would wrap. On success, *stoppedOut is true when hardware stop was already
 * confirmed at the time the operation was counted.
 */
KERNELABI oserr_t KERNELAPI
DMABindingBeginOperation(
    _In_  DMABinding_t* binding,
    _In_  bool          release,
    _Out_ bool*         stoppedOut);

/**
 * @brief Rechecks that a bound context is still live before committing work.
 */
KERNELABI oserr_t KERNELAPI
DMABindingCheckOperation(
    _In_ DMABinding_t* binding);

/**
 * @brief Lowers the active operation count for one finished operation.
 *
 * If the recovery group is blocked and this was the last active operation,
 * triggers another cancellation and event pass so a waiting stop request can
 * proceed.
 */
KERNELABI void KERNELAPI
DMABindingEndOperation(
    _In_ DMABinding_t* binding);

/**
 * @brief Blocks the whole recovery group and cuts off all of its bound contexts.
 *
 * Cancels contexts after dropping manager and group locks.
 */
KERNELABI void KERNELAPI
DMABindingRevoke(
    _In_ DMABinding_t* binding);

/**
 * @brief Blocks an attachment's recovery group from starting DMA and returns a stop ticket.
 *
 * Calling this again while the group is blocked returns the same ticket until
 * the group is cleaned and reused or reshaped. Invalid IDs leave groups unchanged.
 */
KERNELABI oserr_t KERNELAPI
DMAManagerPrepareStop(
    _In_  DMAManager_t*      manager,
    _In_  uint64_t           attachmentID,
    _Out_ OSDmaStopTicket_t* ticketOut);

/**
 * @brief Confirms hardware stop and cleans up every mapping in the ticket's group.
 *
 * Before calling this, physically stop every device in the group so none of
 * them can touch RAM. Wait for hardware confirmation that transfers stopped
 * instead of trusting a driver message or disabled interrupts.
 *
 * Returns OS_EBUSY if system calls are still finishing up or cancellation is
 * running. Keep the hardware stopped and retry with the same ticket after
 * OS_EBUSY or any cleanup error. Cleanup runs outside manager and group locks.
 * Calling this again on an already finished ticket returns OS_EOK without
 * repeating work.
 */
KERNELABI oserr_t KERNELAPI
DMAManagerCompleteStop(
    _In_ DMAManager_t*            manager,
    _In_ const OSDmaStopTicket_t* ticket);

/**
 * @brief Registers a region DMA call as an active operation on a binding.
 *
 * Calls on a region session (starting a transfer or finishing one) must keep
 * the recovery group from tearing down the mapping while the syscall runs.
 * Holding the open region session keeps the mapping and its binding valid.
 *
 * When complete is false (starting new DMA work), the binding, its recovery
 * group, and any attached IOMMU unit must all be live. When complete is true
 * (finishing work that already started), the call is still allowed after the
 * group is blocked or closed, as long as stop cleanup is not currently running.
 *
 * @param binding  Binding attached to the session's device context.
 * @param complete True when finishing an existing transfer, false when starting
 *                 new work.
 * @param counted  Must be false on entry. Set to true when this call adds to
 *                 the group's active operation count, and left false on error.
 * @return OS_EOK on success, OS_EBUSY while stop cleanup is running,
 *         OS_ECANCELLED if the binding or group cannot accept the call, or an
 *         IOMMU failure or counter overflow error.
 */
KERNELABI oserr_t KERNELAPI
DMABindingBeginRegionOperation(
    _In_    DMABinding_t* binding,
    _In_    bool          complete,
    _InOut_ bool*         counted);

/**
 * @brief Removes a region operation from the group's active operation count.
 *
 * Clears *counted to false. If the recovery group is blocked waiting to stop
 * and this was the last running operation, this runs another cancel pass and
 * notifies the manager so stop cleanup can continue. Calling this when *counted
 * is already false does nothing.
 *
 * @param binding Binding passed to DMABindingBeginRegionOperation.
 * @param counted Flag tracking whether this call is currently counted.
 */
KERNELABI void KERNELAPI
DMABindingEndRegionOperation(
    _In_    DMABinding_t* binding,
    _InOut_ bool*         counted);

/**
 * @brief Locks a binding's group spinlock right before flipping a region token active.
 *
 * Cache syncing runs before this call without holding any spinlock. This lock
 * then verifies that the binding, recovery group, and IOMMU unit are still
 * live at the instant the transfer is committed.
 *
 * On success, the spinlock stays held with interrupts disabled, so only fast
 * token metadata updates may run before DMABindingUnlockRegionCommit. On
 * failure, the spinlock is released and interrupts are restored.
 *
 * @param binding Binding attached to the session's device context.
 * @return OS_EOK if live (caller must call DMABindingUnlockRegionCommit), or an
 *         error if the binding, group, or IOMMU unit stopped.
 */
KERNELABI oserr_t KERNELAPI
DMABindingLockRegionCommit(
    _In_ DMABinding_t* binding);

/**
 * @brief Releases the spinlock taken by DMABindingLockRegionCommit and restores interrupts.
 *
 * @param binding Same binding passed to DMABindingLockRegionCommit.
 */
KERNELABI void KERNELAPI
DMABindingUnlockRegionCommit(
    _In_ DMABinding_t* binding);

#endif //!__DMA_ATTACHMENT_H__

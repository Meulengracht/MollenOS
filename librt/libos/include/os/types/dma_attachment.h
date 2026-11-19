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

#ifndef __OS_TYPES_DMA_ATTACHMENT_H__
#define __OS_TYPES_DMA_ATTACHMENT_H__

#include <os/types/dma.h>

#define DMA_ATTACHMENT_VERSION      1U
#define DMA_MANAGER_MAX_ATTACHMENTS 256U
#define DMA_MANAGER_MAX_EVENTS      32U

// Event flags sent to the device manager when an attachment needs attention.
// - DMA_ATTACHMENT_IDLE: the driver closed its binding normally.
// - DMA_ATTACHMENT_STOP_REQUIRED: access was cut off or the driver exited with
//   DMA active, so the manager must physically stop the hardware first.
// - DMA_ATTACHMENT_CLEANUP_FAILED: an earlier stop cleanup hit an error and
//   needs to be retried.
// - DMA_ATTACHMENT_DEVICE_FAULT: the IOMMU refused a DMA access from this
//   device or from one that shares its hardware ID. The access reached no
//   memory. The kernel has not stopped or blocked anything.
// Note that getting an event does not mean the hardware has already stopped.
#define DMA_ATTACHMENT_IDLE           0x1U
#define DMA_ATTACHMENT_STOP_REQUIRED  0x2U
#define DMA_ATTACHMENT_CLEANUP_FAILED 0x4U
#define DMA_ATTACHMENT_DEVICE_FAULT   0x8U

/**
 * @brief Describes a device's DMA setup as discovered by the bus manager.
 *
 * Device and Host are IDs from the device manager (deviced) registry, which
 * differ from kernel handles.
 *
 * Devices that share a bus path or a reset line cannot always be stopped on
 * their own without disturbing their neighbors. RequesterGroup and ResetGroup
 * mark those shared hardware links. Any two attachments with the same
 * RequesterGroup, or the same ResetGroup, are placed into one recovery group
 * so they get stopped together if something goes wrong. Linking also works
 * through a third device (if A shares a requester key with B, and B shares a
 * reset key with C, all three end up in one group). Both keys must be nonzero.
 * The two fields use separate key spaces, so a RequesterGroup of 5 does not
 * match a ResetGroup of 5. They are also separate from hardware IDs and process
 * security IDs.
 *
 * AddressLimit sets the highest device address the hardware can reach
 * (inclusive). CachePolicy must be SHMDeviceCacheCoherent (hardware sees CPU
 * caches on its own) or SHMDeviceCacheNonCoherent (software must flush and
 * invalidate CPU caches around transfers). RangeCount must be 1 through
 * DMA_MAX_RANGES and gives how many entries in Ranges translate physical RAM
 * addresses to device addresses. Reserved and unused Ranges entries must be 0.
 */
typedef struct OSDmaAttachmentParameters {
    uint32_t              Version;
    uint32_t              Reserved;
    uuid_t                Device;
    uuid_t                Host;
    uint64_t              RequesterGroup;
    uint64_t              ResetGroup;
    uint64_t              AddressLimit;
    uint32_t              CachePolicy;
    uint32_t              RangeCount;
    struct SHMDeviceRange Ranges[DMA_MAX_RANGES];
} OSDmaAttachmentParameters_t;

/**
 * @brief Identifies one registered attachment inside a DMA manager.
 * Zero is invalid, and IDs are never reused once freed.
 */
typedef struct OSDmaAttachmentId {
    uint64_t Value;
} OSDmaAttachmentId_t;

/**
 * @brief Event sent to the device manager when an attachment changes state.
 *
 * Attachment identifies which device entry changed inside this manager.
 * Generation identifies which driver binding session the event is for.
 * StopEpoch counts which forced-stop round this event belongs to (it starts at
 * 1 and goes up each time the recovery group is stopped and reused, or stays 0
 * when the driver closed its binding cleanly). Reasons is a bitmask of
 * DMA_ATTACHMENT_* flags, and Reserved is always 0.
 *
 * The kernel can deliver the same event more than once. An event is only a
 * signal to go inspect the device. Always check the device state and verify
 * that hardware transfers have actually stopped before freeing resources.
 */
typedef struct OSDmaAttachmentEvent {
    uint64_t Attachment;
    uint64_t Generation;
    uint64_t StopEpoch;
    uint32_t Reasons;
    uint32_t Reserved;
} OSDmaAttachmentEvent_t;

/**
 * @brief Request to let a driver perform DMA on an attachment.
 *
 * Attachment and RequestId must be nonzero. Principal is the security group ID
 * of the target driver process (the process group that owns its handles). Once
 * the first bind succeeds, only that same Principal can bind to this attachment
 * until the attachment is removed.
 *
 * Send a larger RequestId for each new bind on the same attachment, staying
 * below UINT64_MAX (hitting UINT64_MAX means the counter ran out). If a reply
 * gets lost, resending the exact same RequestId returns the existing binding
 * handle and generation, provided that binding is still active and the driver
 * still holds its handle. Failed bind attempts leave the last successful
 * RequestId unchanged.
 */
typedef struct OSDmaBindParameters {
    uint64_t Attachment;
    uint64_t Principal;
    uint64_t RequestId;
} OSDmaBindParameters_t;

/**
 * @brief Binding handle and generation returned when binding an attachment.
 *
 * The kernel puts Handle straight into the driver's handle table, so the
 * calling manager cannot use that handle itself and passing this struct to
 * another process gives that process no access. Generation is a nonzero counter
 * that never repeats within the same manager. Reserved is always 0.
 */
typedef struct OSDmaBindingResult {
    uuid_t   Handle;
    uint32_t Reserved;
    uint64_t Generation;
} OSDmaBindingResult_t;

/**
 * @brief Options passed by a driver when opening a DMA context from a binding.
 *
 * Generation must match the number returned when the binding was created.
 * AddressLimit lets the driver lower the device's allowed address ceiling (for
 * instance, if a chip revision has a smaller limit), though it can never raise
 * the ceiling set at registration.
 */
typedef struct OSDmaBoundOpenParameters {
    uint64_t Generation;
    uint64_t AddressLimit;
} OSDmaBoundOpenParameters_t;

/**
 * @brief Ticket used to confirm that a recovery group has stopped DMA.
 *
 * PrepareStop fills this in with nonzero RecoveryGroup and Epoch values below
 * UINT64_MAX, tying the stop request to one specific group and stop round so
 * an old ticket cannot affect a newer session. Physically stop every device in
 * that recovery group before passing the ticket to CompleteStop.
 */
typedef struct OSDmaStopTicket {
    uint64_t RecoveryGroup;
    uint64_t Epoch;
} OSDmaStopTicket_t;

#endif //!__OS_TYPES_DMA_ATTACHMENT_H__

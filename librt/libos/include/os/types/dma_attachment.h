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

// These flags tell the service there is work to handle. They do not make it safe
// to release memory.
#define DMA_ATTACHMENT_IDLE           0x1U
#define DMA_ATTACHMENT_STOP_REQUIRED  0x2U
#define DMA_ATTACHMENT_CLEANUP_FAILED 0x4U

/**
 * @brief Fixed DMA route and bus topology from trusted bus discovery.
 * Device and Host are deviced registry IDs, not kernel handles. RequesterGroup
 * and ResetGroup must be nonzero keys within the manager. Each uses a separate
 * namespace, so matching a requester key with a reset key does not connect two
 * attachments.
 * Attachments connected through either kind of key form one recovery group,
 * including attachments connected indirectly through other attachments.
 * These keys are separate from hardware requester IDs and security principals.
 * AddressLimit is the largest device address allowed, inclusive. CachePolicy must
 * be SHMDeviceCacheNonCoherent or SHMDeviceCacheCoherent. RangeCount must be from
 * 1 through DMA_MAX_RANGES and selects the physical-to-device ranges used from
 * Ranges. Reserved and unused range entries must be zero.
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
 * @brief Attachment ID within one manager.
 * Zero is invalid. IDs are never reused.
 */
typedef struct OSDmaAttachmentId {
    uint64_t Value;
} OSDmaAttachmentId_t;

/**
 * @brief Combined event reasons for one attachment and binding generation.
 * Attachment is a manager-local ID. Generation identifies the binding, and
 * StopEpoch identifies a recovery operation (zero for an idle event). Reasons
 * combines DMA_ATTACHMENT_* flags. Reserved is always zero.
 * The same event may be delivered again. Receiving it does not prove that the
 * physical device has stopped.
 */
typedef struct OSDmaAttachmentEvent {
    uint64_t Attachment;
    uint64_t Generation;
    uint64_t StopEpoch;
    uint32_t Reasons;
    uint32_t Reserved;
} OSDmaAttachmentEvent_t;

/**
 * @brief Request to bind an attachment to a driver whose assignment was authenticated.
 * Attachment and RequestId must be nonzero. Principal is the full security-group
 * identity. The first successful binding fixes it until the attachment is removed.
 * Each new request for that attachment must increase RequestId. UINT64_MAX marks
 * exhaustion and cannot be used for a request.
 * Repeating the last published RequestId returns the same handle and generation
 * only while the binding is live and the driver still has its grant. Failed
 * preparation or publication does not replace the last published request.
 */
typedef struct OSDmaBindParameters {
    uint64_t Attachment;
    uint64_t Principal;
    uint64_t RequestId;
} OSDmaBindParameters_t;

/**
 * @brief Receiver's binding handle and the generation fixed when it was created.
 * Reserved is zero. Generations are nonzero and never reused within a manager.
 * Returning this result to another calling group does not give that group a grant.
 */
typedef struct OSDmaBindingResult {
    uuid_t   Handle;
    uint32_t Reserved;
    uint64_t Generation;
} OSDmaBindingResult_t;

#endif //!__OS_TYPES_DMA_ATTACHMENT_H__

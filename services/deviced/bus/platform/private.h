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
 */

#ifndef __DEVICED_PLATFORM_PRIVATE_H__
#define __DEVICED_PLATFORM_PRIVATE_H__

#include <ddk/devices/platform.h>
#include <ddk/resources.h>
#include <os/usched/mutex.h>
#include <stdatomic.h>

#include <bus/platform/platform.h>
#include <core/publication.h>
#include <firmware/tree.h>
#include <dma-types.h>

#include "activation.h"

struct PlatformActivation;
struct PlatformDependencyRegistration;

/**
 * @brief Tracks an interrupt allocation and the process that owns it.
 * The lease keeps activation active until the kernel attachment closes.
 * Attached records a confirmed attachment. An unconfirmed lease may represent a
 * busy group whose owner or kind has not yet been checked; a later terminal
 * rejection can release that pending lease without discarding confirmed ownership.
 */
struct PlatformInterruptLease {
    uuid_t Principal;
    uuid_t Allocation;
    int    Attached;
};

/**
 * @brief One published firmware node and its platform runtime state.
 * Resources owns memory-mapped I/O handles. Activation owns dependency tokens.
 * IRQ and DMA references prevent Stop from releasing dependencies.
 * Bus->Lock serializes these fields.
 */
struct PlatformEntry {
    struct PlatformEntry*         Next;
    struct PlatformBus*           Bus;
    PlatformDevice_t              Firmware;
    uint32_t                      ParentNode;
    int                           IsBus;
    uuid_t                        DeviceId;
    struct DeviceResources        Resources;
    struct PlatformActivation*    Activation;
    uint32_t                      DmaReferences;
    struct PlatformInterruptLease Interrupts[PLATFORM_DEVICE_MAX_INTERRUPTS];
};

/**
 * @brief Owns platform device records, their publication group and firmware tree.
 * Lock protects runtime state and dependency registrations. References counts
 * active device users. Destruction requires these users to release their
 * references and resources.
 */
struct PlatformBus {
    struct FdtTree*                        Tree;
    struct PlatformEntry*                  Entries;
    struct DmPublicationGroup              Publication;
    uuid_t                                 DeviceId;
    atomic_uint                            References;
    struct usched_mtx                      Lock;
    int                                    Removing;
    struct PlatformDependencyRegistration* Dependencies;
};

/**
 * @brief Prepare, start or stop a device for the verified owning process.
 * Repeating a completed stage succeeds without acquiring resources again.
 *
 * @param entry Native device; caller holds Bus->Lock.
 * @param principal Verified nonzero process identity.
 * @param stage Stop requires a halted engine and released IRQ/DMA leases.
 * @return OS_EOK, OS_EBUSY while IRQ or DMA leases are active, or a dependency
 *         callback or validation error. Failed dependency release keeps its tokens
 *         for retry.
 */
__EXTERN oserr_t
PlatformActivationControl(
    _InOut_ struct PlatformEntry* entry,
    _In_    uuid_t                principal,
    _In_    enum DeviceActivation stage);

/**
 * @brief Query fixed sources after checking their controllers with the kernel.
 *
 * @param entry Prepared device; caller holds Bus->Lock.
 * @param buffer DeviceInterruptSourceQuery selecting one firmware index; updated on success.
 * @param length Must equal sizeof(DeviceInterruptSourceQuery).
 * @return OS_EOK, OS_EBUSY before preparation, or validation/controller error.
 */
__EXTERN oserr_t
PlatformInterruptQuery(
    _In_    struct PlatformEntry* entry,
    _InOut_ void*                 buffer,
    _In_    size_t                length);

/**
 * @brief Reserve, enable, disable or release a fixed interrupt source.
 *
 * @param entry Owner of the source list; caller holds Bus->Lock.
 * @param principal Verified process identity, never read from the request body.
 * @param buffer DeviceInterruptSourceRequest; Reserve selects one source by Index.
 * @param length Exact wire request size.
 * @return OS_EOK or a validation, ownership or kernel error. Uncertain attachment
 *         or failed close keeps the lease active and prevents activation teardown.
 */
__EXTERN oserr_t
PlatformInterruptControl(
    _InOut_ struct PlatformEntry* entry,
    _In_    uuid_t                principal,
    _In_    void*                 buffer,
    _In_    size_t                length);

/**
 * @brief Describe reachable RAM and keep activation active for DMA.
 *
 * @param context Prepared PlatformEntry kept alive by the registry request guard.
 * @param principal Authenticated driver, or zero for a trusted internal query.
 * @param description Output changed only on success.
 * @return OS_EOK or a preparation, ownership, firmware or reference-count error.
 *         Each success requires one PlatformReleaseDma call. Both callbacks
 *         acquire Bus->Lock internally.
 */
__EXTERN oserr_t
PlatformPrepareDma(
    _In_  void*                    context,
    _In_  uuid_t                   principal,
    _Out_ struct DmDmaDescription* description);

/**
 * @brief Release one DMA reference without accessing hardware.
 * Call once for each successful PlatformPrepareDma call.
 *
 * @param context PlatformEntry from the matching PlatformPrepareDma call.
 */
__EXTERN void
PlatformReleaseDma(
    _In_ void* context);

/**
 * @brief Collect publishable devices using validated common firmware descriptions.
 *
 * @param bus Empty inventory; partial entries remain owned by bus on failure.
 * @param tree Shared parsed firmware inventory.
 * @return OS_EOK or a description/allocation error.
 */
__EXTERN oserr_t
PlatformCollectDevices(
    _InOut_ struct PlatformBus* bus,
    _In_    struct FdtTree*     tree);

/**
 * @brief Check whether all activation dependencies were acquired.
 *
 * @param entry Device to check; caller holds Bus->Lock.
 * @return Nonzero when preparation completed successfully.
 */
__EXTERN int
PlatformIsActivationReady(
    _In_ const struct PlatformEntry* entry);

/**
 * @brief Check whether activation or interrupt allocations prevent removal.
 *
 * @param entry Device to check; caller holds Bus->Lock.
 * @return Nonzero while activation state or interrupt allocations exist.
 */
__EXTERN int
PlatformIsRuntimeBusy(
    _In_ const struct PlatformEntry* entry);

#endif

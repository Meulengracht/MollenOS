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
 * @brief Interrupt allocation and the process responsible for it.
 * The lease keeps device dependencies in use until the kernel attachment closes.
 * Attached is set once attachment is confirmed. An unconfirmed lease may refer
 * to a busy group whose process or kind has not yet been checked. A later final
 * rejection can release that pending lease. Confirmed attachments must stay intact.
 */
struct PlatformInterruptLease {
    uuid_t Principal;
    uuid_t Allocation;
    int    Attached;
};

/**
 * @brief Published firmware node and its runtime state.
 * Resources stores memory-mapped I/O handles. Activation stores dependency tokens.
 * Stop can release dependencies only after IRQ and DMA references are released.
 * Hold Bus->Lock when accessing these fields.
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
 * @brief Platform device records, publication group and firmware tree.
 * Lock protects runtime state and dependency registrations. References counts
 * active device users. Before destruction, these users must release their
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
 * @brief Prepare, start or stop a device for its verified driver process.
 * Repeating a completed stage succeeds without requesting resources again.
 *
 * @param entry Native device. The caller must hold Bus->Lock.
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
 * @param entry Prepared device. The caller must hold Bus->Lock.
 * @param buffer DeviceInterruptSourceQuery selecting one firmware index. Updated on success.
 * @param length Must equal sizeof(DeviceInterruptSourceQuery).
 * @return OS_EOK, OS_EBUSY before preparation, or a validation or controller error.
 */
__EXTERN oserr_t
PlatformInterruptQuery(
    _In_    struct PlatformEntry* entry,
    _InOut_ void*                 buffer,
    _In_    size_t                length);

/**
 * @brief Reserve, enable, disable or release a fixed interrupt source.
 *
 * @param entry Device containing the source list. The caller must hold Bus->Lock.
 * @param principal Verified process identity, never read from the request body.
 * @param buffer DeviceInterruptSourceRequest. Reserve selects one source by Index.
 * @param length Exact wire request size.
 * @return OS_EOK or a validation, access or kernel error. If attachment
 *         is uncertain or closing fails, the lease stays active and prevents
 *         device dependencies from being released.
 */
__EXTERN oserr_t
PlatformInterruptControl(
    _InOut_ struct PlatformEntry* entry,
    _In_    uuid_t                principal,
    _In_    void*                 buffer,
    _In_    size_t                length);

/**
 * @brief Describe reachable RAM and keep device dependencies in use for DMA.
 *
 * @param context Prepared PlatformEntry kept alive by the registry request guard.
 * @param principal Authenticated driver, or zero for a trusted internal query.
 * @param description Output changed only on success.
 * @return OS_EOK or a preparation, access, firmware or reference-count
 *         error. Call PlatformReleaseDma once for each successful call.
 *         Both callbacks lock Bus->Lock internally.
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
 * @brief Collect devices for publication from validated firmware descriptions.
 *
 * @param bus Empty inventory. On failure, bus keeps any entries already added.
 * @param tree Shared parsed firmware inventory.
 * @return OS_EOK or a description or allocation error.
 */
__EXTERN oserr_t
PlatformCollectDevices(
    _InOut_ struct PlatformBus* bus,
    _In_    struct FdtTree*     tree);

/**
 * @brief Check whether all device dependencies are ready.
 *
 * @param entry Device to check. The caller must hold Bus->Lock.
 * @return Nonzero when preparation completed successfully.
 */
__EXTERN int
PlatformIsActivationReady(
    _In_ const struct PlatformEntry* entry);

/**
 * @brief Check whether device setup or interrupt allocations prevent removal.
 *
 * @param entry Device to check. The caller must hold Bus->Lock.
 * @return Nonzero while device setup state or interrupt allocations exist.
 */
__EXTERN int
PlatformIsRuntimeBusy(
    _In_ const struct PlatformEntry* entry);

/**
 * @brief Validate a prepared MAC's PHY reset request.
 *
 * @param entry Prepared native MAC. The caller must hold Bus->Lock.
 * @param principal Verified process that prepared the device.
 * @param buffer Reserved uint32_t, which must be zero.
 * @param length Exact buffer size.
 * @return OS_EOK for a MAC without a reset GPIO, otherwise a support or state error.
 */
oserr_t
PlatformEthernetReset(
    _InOut_ struct PlatformEntry* entry,
    _In_    uuid_t                principal,
    _In_    const void*           buffer,
    _In_    size_t                length);

#endif //!__DEVICED_PLATFORM_PRIVATE_H__

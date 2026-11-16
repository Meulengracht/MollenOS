/**
 * Copyright 2026, Philip Meulengracht
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
 *
 * Internal DMA state for deviced: routes, bindings and host recovery.
 */

#ifndef __DEVICED_DMA_PRIVATE_H__
#define __DEVICED_DMA_PRIVATE_H__

#include <core/dma.h>
#include <ddk/dma/attachment.h>
#include <os/types/time.h>
#include <os/usched/mutex.h>

/**
 * @brief The steps of stopping a host, in the order they happen.
 * StopPending still needs a ticket from the kernel. StopPrepared has the ticket
 * and needs the bus to stop its hardware. HardwareStopped needs the kernel to
 * confirm the stop. Stopped and StopUnsupported are final until the host's
 * devices are removed.
 */
enum DmDmaHostState {
    DmDmaHostReady,
    DmDmaHostStopPending,
    DmDmaHostStopPrepared,
    DmDmaHostHardwareStopped,
    DmDmaHostStopped,
    DmDmaHostStopUnsupported
};

/**
 * @brief DMA registration and recovery state for one host.
 * All fields are protected by g_dmDmaLock. Device is the root device's registry
 * ID, not a host number that only the bus understands. New DMA work is only
 * allowed in the Ready state. Once a stop has been requested the host stays
 * blocked until its devices are removed, even after the stop has finished.
 */
typedef struct DmDmaHost {
    struct DmDmaHost*   Next;
    uuid_t              Device;
    unsigned int        Devices;
    enum DmDmaHostState State;
    OSDmaStopTicket_t   Ticket;
    OSTimestamp_t       RetryAt;
    unsigned int        RetryDelay;
} DmDmaHost_t;

/**
 * @brief The last binding request for a device, and the lease that keeps its bus alive.
 * RequestId and Principal are kept when the binding goes idle. Result and Lease
 * only describe the current generation. Idle is remembered until any host stop
 * that is needed has finished.
 */
typedef struct DmDmaBinding {
    uint64_t             Principal;
    uint64_t             RequestId;
    OSDmaBindingResult_t Result;
    struct DmDmaLease*   Lease;
    bool                 Idle;
} DmDmaBinding_t;

enum DmDmaRouteState {
    DmDmaRouteUnavailable,
    DmDmaRouteReady,
    DmDmaRouteChanged
};

/**
 * @brief Everything deviced remembers about one device's DMA for its whole lifetime.
 * This is the saved route, the kernel's attachment ID and the binding history.
 * A Ready route with a zero ID means registration can be retried using the saved
 * description. Changed blocks any new binding until the device is removed. If
 * the first registration fails, the kernel side is cancelled, so removal can
 * simply drop a record whose ID is still zero.
 */
typedef struct DmDmaAttachment {
    struct DmDmaAttachment* Next;
    uuid_t                  Device;
    enum DmDmaRouteState    RouteState;
    DmDmaDescription_t      Description;
    OSDmaAttachmentId_t     Id;
    DmDmaHost_t*            Host;
    DmDmaBinding_t          Binding;
} DmDmaAttachment_t;

enum DmDmaManagerState {
    DmDmaManagerNotStarted,
    DmDmaManagerUnavailable,
    DmDmaManagerReady
};

/**
 * @brief All DMA state in the service, protected by g_dmDmaLock.
 * When the manager is Unavailable, all records and bus leases are kept until
 * they can be removed safely.
 */
typedef struct DmDmaState {
    enum DmDmaManagerState ManagerState;
    uuid_t                 Manager;
    unsigned int           DeviceCount;
    DmDmaAttachment_t*     Devices;
    DmDmaHost_t*           Hosts;
} DmDmaState_t;

extern struct usched_mtx g_dmDmaLock;
extern DmDmaState_t      g_dmDmaState;

/**
 * @brief Read pending kernel events. Needs g_dmDmaLock. Does not stop any hardware.
 */
__EXTERN oserr_t
DmDmaDrainEventsLocked(void);

/** 
 * @brief Fail if a different principal has already bound this device. Needs g_dmDmaLock.
 */
__EXTERN oserr_t
DmDmaCheckAssignmentLocked(
    _In_ uuid_t   device,
    _In_ uint64_t principal);

/**
 * @brief Check the driver assignment and, if enableDma is set, the live binding. Needs g_dmDmaLock.
 */
__EXTERN oserr_t
DmDmaCheckControlLocked(
    _In_ uuid_t   device,
    _In_ uint64_t principal,
    _In_ bool     enableDma);

/**
 * @brief Subscribe to kernel events. Call once while the manager starts, before any registration.
 */
__EXTERN oserr_t
DmDmaRecoveryInitialize(
    _In_ uuid_t manager);

#endif //!__DEVICED_DMA_PRIVATE_H__

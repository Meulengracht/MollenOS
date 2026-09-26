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
 *
 * Network Manager
 * - Contains the implementation of the network-manager which keeps track
 *   of sockets, network interfaces and connectivity status
 * 
 * Service-side serialized adapter executor and bounded discovery registry.
 */

#ifndef __NETD_ADAPTERS_H__
#define __NETD_ADAPTERS_H__

#include "adapter.h"

/** 
 * @brief Consumers run on the worker with the registry locked; do not block/reenter.
 * RX data is borrowed only during Receive. Device/port identify the interface.
 */
typedef struct NetworkAdapterOps {
    void (*Receive)(uuid_t device, uint32_t port, const void* data, uint32_t length);
    void (*Transmitted)(uuid_t device, uint32_t port, uint64_t cookie, oserr_t status);
    void (*Link)(uuid_t device, uint32_t port, const struct ctt_netadapter_link* link);
} NetworkAdapterOps_t;

/** 
 * @brief Creates the needed resources for tracking and managing network adapters.
 * This should be called just during startup, and keeps resources until netd shutdown.
 * @return OS_EOK on success, or an error code on failure.
 */
__EXTERN oserr_t
NetworkAdaptersInitialize(void);

/**
 * @brief Called when a new network adapter device is discovered by the service runtime.
 * @param device The UUID of the discovered network adapter device.
 * @param driver The UUID of the driver associated with the discovered network adapter device.
 */
__EXTERN void
NetworkAdaptersDiscover(
    _In_ uuid_t device,
    _In_ uuid_t driver);

/**
 * @brief Cancels any pending attachment or replacement for the specified network adapter 
 * device and requests a safe close on all its ports.
 * @param device The UUID of the network adapter device to remove.
 */
__EXTERN void
NetworkAdaptersRemove(
    _In_ uuid_t device);

/**
 * @brief Copy adapter ops under the registry lock. NULL clears all ops.
 * Callbacks execute on the worker, so hook context must outlive any installed callback.
 * @param ops The set of network adapter operations to install.
 */
__EXTERN void
NetworkAdaptersSetHooks(
    _In_opt_ const NetworkAdapterOps_t* ops);

/**
 * @brief Copy a frame into the selected port queue and wake the worker.
 * @param device The UUID of the network adapter device.
 * @param port The port number of the network adapter device.
 * @param frame Pointer to the frame data to send.
 * @param length Length of the frame data.
 * @param cookie User-defined cookie for tracking the transmission.
 * @return OS_EBUSY if bounded-pool backpressure occurs; OS_ENOENT if no such device/port is registered; OS_EOK if the frame is accepted.
 */
__EXTERN oserr_t
NetworkAdaptersSend(
    _In_ uuid_t      device,
    _In_ uint32_t    port,
    _In_ const void* frame,
    _In_ uint32_t    length,
    _In_ uint64_t    cookie);

/**
 * @brief Returns cached state immediately and schedules an asynchronous counters refresh.
 * An announcement awaiting local allocation reports INFO with zero capabilities.
 * @param device The UUID of the network adapter device.
 * @param port The port number of the network adapter device.
 * @param snapshot Pointer to the structure where the snapshot will be stored.
 * @return OS_EOK if the snapshot is successfully retrieved; otherwise, an error code.
 */
__EXTERN oserr_t
NetworkAdaptersSnapshot(
    _In_ uuid_t                 device,
    _In_ uint32_t               port,
    _Out_ NetAdapterSnapshot_t* snapshot);

/**
 * @brief Start or stop the specified network adapter port.
 * @param device The UUID of the network adapter device.
 * @param port The port number of the network adapter device.
 * @param start true resumes a stopped adapter; false requests a drain/stop barrier.
 * @return OS_EBUSY while initial local attachment is pending; otherwise, an error code.
 */
__EXTERN oserr_t
NetworkAdaptersSetRunning(
    _In_ uuid_t   device,
    _In_ uint32_t port,
    _In_ bool     start);

/**
 * @brief Request asynchronous safe close. Observe completion through Snapshot; failure
 * to prove remote retirement retains buffers in quarantine rather than freeing them.
 * @param device The UUID of the network adapter device.
 * @param port The port number of the network adapter device.
 * @return OS_EOK if the close request is successfully initiated; otherwise, an error code.
 */
__EXTERN oserr_t
NetworkAdaptersClose(
    _In_ uuid_t   device,
    _In_ uint32_t port);

/**
 * @brief Resume recovery for a quarantined port without resetting descriptor identity.
 * Does not imply the device has reset or that outstanding DMA has stopped.
 * @param device The UUID of the network adapter device.
 * @param port The port number of the network adapter device.
 * @return OS_EOK if the recovery is successfully initiated; otherwise, an error code.
 */
__EXTERN oserr_t
NetworkAdaptersRetry(
    _In_ uuid_t   device,
    _In_ uint32_t port);

#ifdef VALI_NET_ADAPTER_TESTS
__EXTERN oserr_t
NetworkAdaptersTestAttach(
    _In_ uuid_t device,
    _In_ uuid_t driver);
void
NetworkAdaptersRunIpcTests(void);
#endif

#ifdef VALI_VIRTIO_NET_TESTS
__EXTERN oserr_t
NetworkAdaptersTestFindVirtual(
    _Out_ uuid_t* deviceOut);
void
NetworkAdaptersRunVirtioTests(void);
#endif
#endif

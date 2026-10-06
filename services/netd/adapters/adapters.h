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
 * Public netd adapter registry, callbacks, and packet APIs.
 * 
 * netd keeps a table of every network adapter port it has discovered. A single
 * worker thread does all the work for these adapters, one step at a time, so the
 * functions in this header only record what should happen and wake that worker.
 */

#ifndef __NETD_ADAPTERS_H__
#define __NETD_ADAPTERS_H__

#include "adapter.h"

/** 
 * @brief Callbacks a consumer installs to hear about adapter events. They are called
 * from the adapter worker thread while the registry lock is held, so they must not
 * block and must not call back into the NetworkAdapters* functions, which would try
 * to take the same lock again.
 * The frame passed to Receive is only lent to the consumer for the duration of the
 * call. ReceivePacket instead lets the consumer keep the packet, and the consumer then
 * manages it until it calls NetworkAdaptersRxRelease. Each callback is told which
 * interface the event belongs to through the device UUID and port number.
 */
typedef struct NetworkAdapterOps {
    void (*Receive)(uuid_t device, uint32_t port, const void* data, uint32_t length);
    void (*Transmitted)(uuid_t device, uint32_t port, uint64_t cookie, oserr_t status);
    void (*Link)(uuid_t device, uint32_t port, const struct ctt_netadapter_link* link);
    
    /**
     * @brief Return true after storing the offered packet in a consumer queue of
     * limited size; the consumer is then responsible for releasing it. Returning
     * false declines it. Do not reenter the registry from here. If the packet cannot
     * be kept, Receive is used instead and only lends the frame during the call.
     */
    bool (*ReceivePacket)(uuid_t device, uint32_t port, const NetAdapterRxPacket_t* packet);
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
 * @brief Called when a network adapter device goes away. Any driver that was waiting to
 * be attached to the device, or waiting to replace the current driver, is forgotten.
 * Every open port of the device is asked to close. The worker finishes the close in
 * the background and only frees a port's resources once the driver has confirmed it
 * no longer uses the port's buffers.
 * @param device The UUID of the network adapter device to remove.
 */
__EXTERN void
NetworkAdaptersRemove(
    _In_ uuid_t device);

/**
 * @brief Install the consumer callbacks for all adapters. The structure is copied while
 * the registry lock is held, so the caller does not need to keep it around. Passing
 * NULL removes all callbacks. The callbacks are later called from the worker thread, so
 * any data they rely on must stay valid for as long as they remain installed.
 * @param ops The set of network adapter operations to install.
 */
__EXTERN void
NetworkAdaptersSetHooks(
    _In_Opt_ const NetworkAdapterOps_t* ops);

/**
 * @brief Copy a frame into the selected port queue and wake the worker.
 * @param device The UUID of the network adapter device.
 * @param port The port number of the network adapter device.
 * @param frame Pointer to the frame data to send.
 * @param length Length of the frame data.
 * @param cookie User-defined cookie for tracking the transmission.
 * @return OS_EBUSY if all TX pool slots are currently in use; OS_ENOENT if no port with
 *         this device and port number is registered; OS_EOK if the frame is accepted.
 */
__EXTERN oserr_t
NetworkAdaptersSend(
    _In_ uuid_t      device,
    _In_ uint32_t    port,
    _In_ const void* frame,
    _In_ uint32_t    length,
    _In_ uint64_t    cookie);

/**
 * @brief Reserve a TX slot from the adapter's packet pool so the caller can write a
 * frame directly into it, avoiding the extra copy done by NetworkAdaptersSend. The
 * frame can be written without holding any netd lock.
 *
 * Only the caller may write to Data, and only until the packet is passed to
 * NetworkAdaptersTxSubmit or NetworkAdaptersTxCancel. Acquire, Submit and Cancel take
 * the registry lock themselves, so they must never be called from an adapter callback,
 * which already runs with that lock held. If the packet is handed to another thread,
 * the caller must synchronize that handover and make sure only one thread uses the
 * packet afterwards.
 *
 * If the adapter is stopped or removed while the packet is being written, the memory
 * stays valid, but submitting it will fail. Every acquired packet must therefore be
 * either submitted or cancelled, including on error paths. While a packet is held,
 * the adapter's buffers cannot be freed, which also delays replacing its driver.
 * @param device The UUID of the network adapter device.
 * @param port The port number of the network adapter device.
 * @param packet Pointer to the TX packet structure to be acquired.
 * @return OS_EOK if the packet was successfully acquired.
 *         OS_EBUSY if the adapter is not ready.
 *         OS_ENOENT if no port with this device and port number is found.
 *         OS_EINVALPARAMS if the packet pointer is NULL.
 */
__EXTERN oserr_t
NetworkAdaptersTxAcquire(
    _In_ uuid_t device, _In_ uint32_t port, _Out_ NetAdapterTxPacket_t* packet);

/**
 * @brief Queue a frame written into a packet from NetworkAdaptersTxAcquire for
 * transmission. The frame is not copied; the worker sends the pool slot itself.
 * On failure the packet is still managed by the caller, who must submit it again or
 * cancel it. On success the packet structure is cleared, and the Transmitted callback
 * later reports the result with the given cookie, just like NetworkAdaptersSend.
 * @param device The UUID of the network adapter device.
 * @param port The port number of the network adapter device.
 * @param packet Pointer to the TX packet structure to be submitted.
 * @param length The length of the data in the packet.
 * @param cookie A user-defined value that will be passed back in the completion callback.
 * @return OS_EOK if the packet was successfully submitted.
 *         OS_EBUSY if the adapter is not ready.
 *         OS_ENOENT if no port with this device and port number is found.
 *         OS_EINVALPARAMS if any of the parameters are invalid.
 */
__EXTERN oserr_t
NetworkAdaptersTxSubmit(
    _In_ uuid_t                   device,
    _In_ uint32_t                 port,
    _InOut_ NetAdapterTxPacket_t* packet,
    _In_ uint32_t                 length,
    _In_ uint64_t                 cookie);

/**
 * @brief Give back a packet from NetworkAdaptersTxAcquire that will not be sent. This
 * still works after the adapter has been removed or closed, because the port entry is
 * kept until every held packet has been returned.
 * @param device The UUID of the network adapter device.
 * @param port The port number of the network adapter device.
 * @param packet Pointer to the TX packet structure to be cancelled.
 * @return OS_EOK if the packet was successfully cancelled.
 *         OS_EBUSY if the adapter is not ready.
 *         OS_ENOENT if no port with this device and port number is found.
 *         OS_EINVALPARAMS if any of the parameters are invalid.
 * Successful cancellation clears the packet and produces no TX callback.
 */
__EXTERN oserr_t
NetworkAdaptersTxCancel(
    _In_ uuid_t                   device,
    _In_ uint32_t                 port,
    _InOut_ NetAdapterTxPacket_t* packet
);

/**
 * @brief Release a packet the consumer kept by returning true from ReceivePacket.
 * Must be called outside adapter callbacks, since it takes the registry lock. It may be
 * called from any consumer thread once that thread has finished reading the data.
 * The worker is woken afterwards so it can give the freed receive buffer back to the
 * driver.
 * A kept packet stays readable even if the adapter is stopped, removed or closed in
 * the meantime, but as long as it is held the adapter cannot be fully destroyed or
 * replaced. On success the packet structure is cleared. Do not make copies of the
 * structure that more than one piece of code might release, and do not release it
 * while anything may still read Data.
 * @param device The UUID of the network adapter device.
 * @param port The port number of the network adapter device.
 * @param packet Pointer to the RX packet structure to be released.
 * @return OS_EOK if the packet was successfully released; otherwise, an error code.
 */
__EXTERN oserr_t
NetworkAdaptersRxRelease(
    _In_ uuid_t                   device,
    _In_ uint32_t                 port,
    _InOut_ NetAdapterRxPacket_t* packet);

/**
 * @brief Returns the last known state of the port right away, and asks the worker to
 * fetch fresh traffic counters from the driver in the background; those show up in a
 * later snapshot. A port that has been discovered but whose adapter has not been
 * created yet reports the NET_ADAPTER_INFO state with all other fields zeroed.
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
 * @param start true resumes a stopped adapter; false asks the adapter to stop. Stopping
 *              happens in the background: work already handed to the driver is allowed
 *              to finish before the adapter is considered stopped.
 * @return OS_EBUSY if the port has been discovered but its adapter has not been created
 *         yet; otherwise OS_EOK on success or an error code.
 */
__EXTERN oserr_t
NetworkAdaptersSetRunning(
    _In_ uuid_t   device,
    _In_ uint32_t port,
    _In_ bool     start);

/**
 * @brief Ask the port to close. The close finishes in the background; use Snapshot to
 * see when the state reaches closed. Buffers are only freed once the driver confirms it
 * has stopped using them. If that confirmation never arrives, the port is put in the
 * quarantined state and its buffers are kept, because the device might still write into
 * them.
 * @param device The UUID of the network adapter device.
 * @param port The port number of the network adapter device.
 * @return OS_EOK if the close request is successfully initiated; otherwise, an error code.
 */
__EXTERN oserr_t
NetworkAdaptersClose(
    _In_ uuid_t   device,
    _In_ uint32_t port);

/**
 * @brief Try again with a port that got stuck in the quarantined state. The open or close
 * request that the driver never confirmed is sent again, using the same session and
 * buffers as before, so the driver can still match it to what it already knows.
 * Calling this does not mean the device was reset, or that it has stopped writing into
 * buffers it was given earlier. A port that failed before a session with the driver was
 * set up instead starts over by asking the driver for its capabilities again.
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

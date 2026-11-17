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
 * worker thread handles all communication and state changes for these adapters,
 * one step at a time. Because of that, the functions here take the registry lock,
 * record what the caller wants, and wake the worker thread.
 *
 * Each active session gets a unique, non-repeating instance ID. Passing that
 * instance ID to packet calls prevents an operation meant for an old driver
 * from hitting a newer driver on the same device and port.
 */

#ifndef __NETD_ADAPTERS_H__
#define __NETD_ADAPTERS_H__

#include "adapter.h"

/** 
 * @brief Callbacks installed by the network stack to receive adapter events.
 *
 * The worker thread invokes these callbacks while holding the registry lock, so
 * callbacks must never block or call back into NetworkAdapters* functions. Doing
 * so would try to lock the same mutex twice.
 *
 * Receive lends the frame pointer only for the duration of the callback.
 * ReceivePacket lets the consumer keep the packet after returning, until it
 * calls NetworkAdaptersRxRelease. Every callback passes device, port, and a
 * non-repeating instance ID so events from a replaced driver are never confused
 * with its replacement.
 */
typedef struct NetworkAdapterOps {
    /**
     * @brief Deliver a received Ethernet frame when ReceivePacket is unset or
     * declines it. Data is only valid until this callback returns.
     */
    void (*Receive)(uuid_t device, uint32_t port, uint64_t instance, const void* data, uint32_t length);

    /**
     * @brief Report the outcome of a transmit request once hardware finishes
     * reading it, or when it is rejected or cancelled.
     */
    void (*Transmitted)(uuid_t device, uint32_t port, uint64_t instance, uint64_t cookie, oserr_t status);

    /**
     * @brief Report a newer link status change (carrier, speed, or duplex).
     */
    void (*Link)(uuid_t device, uint32_t port, uint64_t instance, const struct ctt_netadapter_link* link);

    /**
     * @brief Report a state change together with the session's instance ID.
     *
     * The state callback for a new instance always arrives before any packet
     * callbacks for that instance. Terminal states (closed, failed, or
     * quarantined) mean the instance is shutting down or stuck. Installing hooks
     * also triggers an immediate state report for every adapter already present.
     */
    void (*State)(uuid_t device, uint32_t port, uint64_t instance, const NetAdapterStatus_t* status);
    
    /**
     * @brief Offer a received packet for the consumer to keep and release later.
     * Return true after storing the packet in a bounded consumer queue. Return
     * false to decline it, which falls back to calling Receive for this frame.
     * Never call back into NetworkAdapters* from here.
     */
    bool (*ReceivePacket)(uuid_t device, uint32_t port, uint64_t instance, const NetAdapterRxPacket_t* packet);
} NetworkAdapterOps_t;

/** 
 * @brief Initialize the global adapter registry and start its worker thread.
 * Call this once during netd startup. Resources stay alive until netd exits.
 * @return OS_EOK on success, OS_EEXISTS if already initialized, or an OS error.
 */
__EXTERN oserr_t
NetworkAdaptersInitialize(void);

/**
 * @brief Register a newly discovered network adapter device and its driver.
 * Port 0 is queued first, and any extra ports on the device are registered once
 * the driver reports its port count.
 * @param device UUID of the discovered network device.
 * @param driver UUID of the driver managing the device.
 */
__EXTERN void
NetworkAdaptersDiscover(
    _In_ uuid_t device,
    _In_ uuid_t driver);

/**
 * @brief Remove a network adapter device and ask its open ports to close.
 * Any driver waiting to attach to this device is dropped right away. Active
 * ports close in the background, and their buffers are only freed once the
 * driver confirms it has stopped using them.
 * @param device UUID of the device being removed.
 */
__EXTERN void
NetworkAdaptersRemove(
    _In_ uuid_t device);

/**
 * @brief Install or clear the consumer callback table for all adapters.
 * The struct is copied under the registry lock, so the caller can pass a stack
 * variable. Passing null clears all hooks. Any context or data used by the
 * callbacks must stay valid while hooks remain installed.
 * @param ops Callback table to install, or null to clear.
 */
__EXTERN void
NetworkAdaptersSetHooks(
    _In_Opt_ const NetworkAdapterOps_t* ops);

/**
 * @brief Copy a frame into a TX buffer, queue it on the port, and wake the worker.
 * @param device Network device UUID.
 * @param port Port index on the device.
 * @param instance Session instance ID. A stale instance returns OS_ENOENT.
 * @param frame Start of the Ethernet frame bytes to send.
 * @param length Frame length in bytes.
 * @param cookie Caller value passed back to Transmitted on completion.
 * @return OS_EOK if queued, OS_EBUSY if all TX slots are busy or the port is
 *         still attaching, OS_ENOENT if no matching instance exists, or a
 *         validation error.
 */
__EXTERN oserr_t
NetworkAdaptersSend(
    _In_ uuid_t      device,
    _In_ uint32_t    port,
    _In_ uint64_t    instance,
    _In_ const void* frame,
    _In_ uint32_t    length,
    _In_ uint64_t    cookie);

/**
 * @brief Reserve a TX slot so the caller can write a frame directly into shared
 * memory without copying it first.
 *
 * The caller can fill Data without holding any netd lock. Only the caller may
 * write to Data until the packet is passed to NetworkAdaptersTxSubmit or
 * NetworkAdaptersTxCancel. Because these functions take the registry lock,
 * never call them from an adapter callback. If another thread writes to the
 * packet, synchronize that handoff yourself.
 *
 * If the adapter closes while you are building the frame, the buffer memory
 * stays valid, though submitting will fail. Every acquired packet must end in
 * either Submit or Cancel. Holding an unsubmitted packet keeps the port's pools
 * alive and blocks a replacement driver from attaching.
 *
 * @param device Network device UUID.
 * @param port Port index on the device.
 * @param instance Session instance ID. A stale instance returns OS_ENOENT.
 * @param packet Output packet handle. Cleared on failure.
 * @return OS_EOK on success, OS_EBUSY if the port is still attaching or out of
 *         TX slots, OS_ENOENT if no matching instance exists, OS_ENOTCONNECTED
 *         if TX is stopped, or OS_EINVALPARAMS if packet is null.
 */
__EXTERN oserr_t
NetworkAdaptersTxAcquire(
    _In_ uuid_t                 device,
    _In_ uint32_t               port,
    _In_ uint64_t               instance,
    _Out_ NetAdapterTxPacket_t* packet);

/**
 * @brief Submit a packet from NetworkAdaptersTxAcquire for transmission without
 * copying its data.
 *
 * On failure, the caller still holds packet and must retry or cancel it. On
 * success, packet is cleared and Transmitted later reports the outcome with
 * cookie, just like NetworkAdaptersSend.
 *
 * @param device Network device UUID.
 * @param port Port index on the device.
 * @param instance Session instance ID. A stale instance returns OS_ENOENT.
 * @param packet Acquired TX packet handle. Cleared on success.
 * @param length Total frame length in bytes, including the Ethernet header.
 * @param cookie Caller value passed back to Transmitted on completion.
 * @return OS_EOK on success, OS_ENOENT if no matching instance or valid handle
 *         exists, OS_ENOTCONNECTED if the run ended or link dropped, or
 *         OS_EINVALPARAMS for invalid arguments or frame length.
 */
__EXTERN oserr_t
NetworkAdaptersTxSubmit(
    _In_ uuid_t                   device,
    _In_ uint32_t                 port,
    _In_ uint64_t                 instance,
    _InOut_ NetAdapterTxPacket_t* packet,
    _In_ uint32_t                 length,
    _In_ uint64_t                 cookie);

/**
 * @brief Cancel a packet from NetworkAdaptersTxAcquire without sending it.
 * Still succeeds after the adapter closes or is removed, because the registry
 * keeps the port entry alive until every held packet is returned. Cancelling
 * clears packet and triggers no Transmitted callback.
 *
 * @param device Network device UUID.
 * @param port Port index on the device.
 * @param instance Session instance ID. A stale instance returns OS_ENOENT.
 * @param packet Acquired TX packet handle. Cleared on success.
 * @return OS_EOK on success, OS_ENOENT if no matching instance or valid handle
 *         exists, or OS_EINVALPARAMS for invalid arguments.
 */
__EXTERN oserr_t
NetworkAdaptersTxCancel(
    _In_ uuid_t                   device,
    _In_ uint32_t                 port,
    _In_ uint64_t                 instance,
    _InOut_ NetAdapterTxPacket_t* packet
);

/**
 * @brief Release an RX packet that the consumer kept by returning true from
 * ReceivePacket.
 *
 * Call this from any consumer thread once it finishes reading Data, outside
 * adapter callbacks. Releasing wakes the worker so the freed receive buffer can
 * go back to the driver. A kept packet stays readable even if the adapter stops
 * or closes, though holding it delays freeing the port's memory or attaching a
 * new driver. Do not copy the packet handle or release it twice, and never read
 * Data after releasing. On success, packet is cleared.
 *
 * @param device Network device UUID.
 * @param port Port index on the device.
 * @param instance Session instance ID. A stale instance returns OS_ENOENT.
 * @param packet Accepted RX packet handle. Cleared on success.
 * @return OS_EOK on success, OS_ENOENT if stale or already released, or an
 *         OS error code.
 */
__EXTERN oserr_t
NetworkAdaptersRxRelease(
    _In_ uuid_t                   device,
    _In_ uint32_t                 port,
    _In_ uint64_t                 instance,
    _InOut_ NetAdapterRxPacket_t* packet);

/**
 * @brief Read the port's cached state and queue a background counter refresh.
 * Returns immediately with cached data. Fresh hardware counters appear in a
 * later snapshot once the worker polls the driver. If a port was discovered but
 * its adapter object has not been created yet, snapshot reports NET_ADAPTER_INFO
 * with all other fields zeroed.
 *
 * @param device Network device UUID.
 * @param port Port index on the device.
 * @param snapshot Output snapshot struct.
 * @return OS_EOK on success, OS_ENOENT if the port is unknown, or an OS error.
 */
__EXTERN oserr_t
NetworkAdaptersSnapshot(
    _In_ uuid_t                 device,
    _In_ uint32_t               port,
    _Out_ NetAdapterSnapshot_t* snapshot);

/**
 * @brief Start or stop a network adapter port.
 * Stopping runs in the background so packets already sent to the driver can
 * finish before the adapter enters NET_ADAPTER_STOPPED.
 *
 * @param device Network device UUID.
 * @param port Port index on the device.
 * @param start true to start a stopped port, false to stop an active one.
 * @return OS_EOK on success, OS_EBUSY if the port is still attaching or cannot
 *         start in its current state, or OS_ENOENT if the port is unknown.
 */
__EXTERN oserr_t
NetworkAdaptersSetRunning(
    _In_ uuid_t   device,
    _In_ uint32_t port,
    _In_ bool     start);

/**
 * @brief Begin closing a port in the background.
 * Poll NetworkAdaptersSnapshot to see when the state reaches NET_ADAPTER_CLOSED.
 * Shared memory is only freed once the driver confirms it stopped using it. If
 * that reply never arrives, the port enters NET_ADAPTER_QUARANTINED and keeps
 * its buffers allocated so the device cannot corrupt freed memory.
 *
 * @param device Network device UUID.
 * @param port Port index on the device.
 * @return OS_EOK if close started, OS_ENOENT if the port is unknown, or an OS error.
 */
__EXTERN oserr_t
NetworkAdaptersClose(
    _In_ uuid_t   device,
    _In_ uint32_t port);

/**
 * @brief Retry a port stuck in NET_ADAPTER_QUARANTINED or a recoverable failure.
 * For a quarantined port, the unanswered OPEN or CLOSE request is sent again
 * with the same session and buffer IDs. Retrying does not reset the hardware
 * or prove it stopped using earlier buffers. If the port failed before any
 * session opened, retry restarts capability discovery from the beginning.
 *
 * @param device Network device UUID.
 * @param port Port index on the device.
 * @return OS_EOK if recovery started, OS_EBUSY if still attaching, OS_ENOENT if
 *         unknown, or OS_EINVALPARAMS if the current state cannot be retried.
 */
__EXTERN oserr_t
NetworkAdaptersRetry(
    _In_ uuid_t   device,
    _In_ uint32_t port);

#ifdef VALI_NET_ADAPTER_TESTS
/**
 * @brief Register a fake or test driver on port 0 for IPC integration tests.
 * @param device Test device UUID.
 * @param driver Test driver UUID.
 * @return OS_EOK if registered, or an OS error code.
 */
__EXTERN oserr_t
NetworkAdaptersTestAttach(
    _In_ uuid_t device,
    _In_ uuid_t driver);

/**
 * @brief Entry point for the built-in netadapter IPC test suite.
 */
void
NetworkAdaptersRunIpcTests(void);
#endif

#ifdef VALI_VIRTIO_NET_TESTS
/**
 * @brief Find the first attached virtual network adapter for Virtio tests.
 * @param deviceOut Receives the matching device UUID.
 * @return OS_EOK if found, OS_ENOENT if none, or OS_EINVALPARAMS for null input.
 */
__EXTERN oserr_t
NetworkAdaptersTestFindVirtual(
    _Out_ uuid_t* deviceOut);

/**
 * @brief Entry point for the built-in Virtio hardware acceptance tests.
 */
void
NetworkAdaptersRunVirtioTests(void);
#endif
#endif

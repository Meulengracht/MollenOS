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
 * Private integration boundary for netd's serialized adapter worker.
 *
 * Registry membership belongs to adapters.c. Transport callbacks execute under
 * its lock and must not acquire it again. Runtime discovery callbacks enter via
 * the public NetworkAdaptersDiscover/Remove functions, which acquire that lock.
 * Each port owns a Gracht client. Client identity selects the local session, but
 * is not peer authentication. Protocol trust policy is deferred to libgracht.
 */

#ifndef __NETD_ADAPTER_PRIVATE_H__
#define __NETD_ADAPTER_PRIVATE_H__

#include "adapters.h"
#include <gracht/link/vali.h>

/** 
 * @brief Maximum number of network adapters that can be registered in the manager.
 */
#define NET_ADAPTER_LIMIT 16

/**
 * @brief Work budget per transport poll cycle - limits frames processed 
 * per adapter per poll.
 */
#define NET_ADAPTER_WORK_BUDGET 16

/**
 * @brief Represents a network adapter in the manager.
 */
struct AdapterEntry {
    NetworkAdapter_t*             Adapter;
    uuid_t                        Device;
    uuid_t                        Driver;
    uint32_t                      Port;
    int                           Endpoint;
    uint64_t                      Serial;
    uint32_t                      SentFrames; // reserve transport IDs for safe close before wrap
    gracht_client_t*              Client;
    struct vali_link_message      Context;
    bool                          AwaitReply;
    uint64_t                      Now;
    uint8_t                       Operation;
    struct ctt_netadapter_session Session;
    bool                          Removed;
    uuid_t                        PendingDriver; // Latest announcement, retained through safe close.
    uint64_t                      AttachAfter;   // Backoff for local allocation failures.
    enum NetAdapterState          LastState;
};

/** 
 * @brief Register a port or request safe close if its advertised driver has changed.
 * Requires the registry lock. Duplicates do not create a second session.
 * @param device The unique identifier of the network device.
 * @param driver The unique identifier of the network driver.
 * @param port   The port number on the device.
 */
__EXTERN void
NetAdapterRegistryDiscover(
    _In_ uuid_t   device,
    _In_ uuid_t   driver,
    _In_ uint32_t port);

/** 
 * @brief Mark every port of a device removed; storage survives until safe close.
 * @param device The unique identifier of the network device.
 */
__EXTERN void
NetAdapterRegistryRemove(uuid_t device);

/** 
 * @brief Retrieve an entry for a generated callback on its dedicated client. This
 * should be called only in locked context.
 * @param client The client associated with the adapter entry.
 * @return The adapter entry corresponding to the client, or NULL if not found.
 */
__EXTERN struct AdapterEntry*
NetAdapterRegistryFindClient(gracht_client_t* client);

/** 
 * @brief Create an adapter client and register its descriptor with the I/O set.
 * Success transfers ownership to the caller; failure releases all resources.
 * @param eventSet The I/O set to register the client with.
 * @param protocol The protocol to use for the client.
 * @param clientOut Pointer to receive the created client.
 * @return 0 on success, or an error code on failure.
 */
__EXTERN int
NetAdapterClientCreate(
    _In_  int                eventSet,
    _In_  gracht_protocol_t* protocol,
    _Out_ gracht_client_t**  clientOut);

/** 
 * @brief Remove readiness registration before shutting down an owned client.
 * @param eventSet The I/O set the client was registered with.
 * @param client The client to destroy.
 */
__EXTERN void
NetAdapterClientDestroy(
    _In_ int              eventSet,
    _In_ gracht_client_t* client);

/** 
 * @brief Pump one port's IPC and publications.
 * @param entry The adapter entry to poll.
 * @param now The current time for scheduling purposes.
 * @return True if the work budget was spent, false otherwise.
 */
__EXTERN bool
NetAdapterTransportPoll(
    _In_ struct AdapterEntry* entry,
    _In_ uint64_t             now);

#endif //!__NETD_ADAPTER_PRIVATE_H__

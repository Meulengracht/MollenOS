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
 * Internal registry entries and adapter worker state.
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
 * @brief Default Maximum Transmission Unit (MTU) for network adapters.
 */
#define NET_ADAPTER_MTU_DEFAULT 1500

/** A batch survives admission until driver retirement is confirmed. Keeping its
 * descriptor identities and results prevents a late duplicate from referring to
 * a newly reused pool slot. All storage is fixed and charged to the budget.
 */
struct AdapterBatch {
    NetAdapterRequest_t Request;
    oserr_t             Results[NET_ADAPTER_BATCH_MAX];
    uint64_t            Deadline;
    uint32_t            Attempts;
    bool                Used;
    bool                Admitted;
};

/** Copy fallback is allocated once per session. Sequence never wraps, so an
 * old release cannot return a subsequently reused copy slot.
 */
struct AdapterRxCopy {
    uint64_t Sequence;
    bool     Retained;
};

struct NetworkAdapter {
    uuid_t                         Device;
    uuid_t                         Driver;
    uint32_t                       Port;
    uint32_t                       Mtu;
    uint32_t                       BatchSize;
    uint32_t                       RxTarget;
    NetAdapterConfig_t             Config;
    NetAdapterCallbacks_t          Callbacks;
    enum NetAdapterState           State;
    oserr_t                        LastError;
    struct ctt_netadapter_info     Info;
    struct ctt_netadapter_session  Session;
    struct ctt_netadapter_link     Link;
    struct ctt_netadapter_counters Counters;
    // Shared packet storage and per-slot callbacks are owned until safe close.
    NetBufferManager_t*  Buffers;
    struct AdapterLease* Leases[2];
    uint32_t             Slots[2];
    uint32_t             RxRetentionLimit;
    uint32_t             RxPoolRetained;
    uint32_t             RxCopyCount;
    uint32_t             RxCopyRetained;
    struct AdapterRxCopy* RxCopies;
    unsigned char*       RxCopyBytes;
    uint64_t             RxFallbackCopies;
    uint64_t             RxDropped;
    // One pending control request; Output is borrowed scratch for ACK/drain.
    NetAdapterRequest_t Request;
    NetAdapterRequest_t Output;
    struct AdapterBatch Batches[NET_ADAPTER_WINDOW_MAX];
    uint32_t            Window;
    // Protocol cursors: admission is contiguous; retirement releases replay slots.
    uint64_t Run;
    uint64_t Admitted;
    uint64_t Retired;
    uint64_t Highest;
    uint64_t DrainId;
    uint64_t DrainAfter;
    uint64_t DrainThrough;
    // A drain snapshot may arrive as chunks and an independently delayed end.
    uint64_t                  DrainDeadline;
    uint64_t                  AckDeadline;
    uint64_t                  DrainSeenThrough;
    uint64_t                  DrainHighest;
    uint32_t                  DrainAttempts;
    uint32_t                  AckAttempts;
    bool                      Draining;
    bool                      DrainEnded;
    bool                      PreferRx;
    struct ctt_netadapter_ack SentAck;
    uint64_t                  NextSerial;
    uint64_t                  NextBatch;
    uint64_t                  StopBarrier;
    uint64_t                  QueueOrder;
    uint64_t                  Deadline;
    uint64_t                  NextPoll;
    uint64_t                  RxRetryAt;
    uint32_t                  Attempts;
    uint32_t                  RxFailures;
    // Intent flags are serviced by the scheduler, never by blocking API calls.
    bool Pending;
    bool StopRequested;
    bool StopReplied;
    bool CloseRequested;
    bool DrainNeeded;
    bool LinkNeeded;
    bool CountersNeeded;
};

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

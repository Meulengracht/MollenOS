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
 * This header is shared only by the files that make up netd's adapter worker and is
 * not a public interface.
 *
 * The list of registered adapters is managed by adapters.c and protected by its lock.
 * Transport callbacks already run with that lock held and must not take it again.
 * Device discovery callbacks come in through the public NetworkAdaptersDiscover and
 * NetworkAdaptersRemove functions, which take the lock themselves.
 * Each port has its own Gracht client. The client a message arrives on decides which
 * local session it belongs to, but that does not prove who sent it. Deciding whether
 * the peer can be trusted is left to libgracht.
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

/**
 * @brief This structure contains a stored copy of a single RX or TX batch, so it can be
 * sent again unchanged if no answer arrives. The batch is kept after the driver has
 * accepted it (admitted it), and is only freed once the driver confirms it is
 * completely finished with it (has retired it). Keeping the packet descriptors and
 * their results until then means a late duplicate answer is matched against this
 * batch, and cannot be mistaken for a newer packet that reuses the same pool slot.
 * The batch storage has a fixed size and is counted against the adapter's memory
 * budget.
 */
struct AdapterBatch {
    NetAdapterRequest_t Request;
    oserr_t             Results[NET_ADAPTER_BATCH_MAX];
    uint64_t            Deadline;
    uint32_t            Attempts;
    bool                Used;
    bool                Admitted;
};

enum {
    NET_ADAPTER_TX = 0,
    NET_ADAPTER_RX = 1
};

/**
 * @brief This structure contains the lease table of a single direction's pool,
 * indexed by NET_ADAPTER_TX/NET_ADAPTER_RX.
 */
struct AdapterQueue {
    struct AdapterLease* Leases;
    uint32_t             Slots;
};

/**
 * @brief This structure contains TX-specific state. QueueOrder is the order number given
 * to the next submitted frame, so frames are sent oldest first even after pool slots
 * have been reused in a different order.
 */
struct AdapterTx {
    uint64_t QueueOrder;
};

/**
 * @brief This structure contains the state of a single RX copy slot. Copy slots are
 * allocated once per session. Sequence increases every time the slot is reused and
 * never wraps around (a slot whose Sequence reaches UINT64_MAX is no longer used), so
 * an old release token cannot free the slot after it has been given to a newer packet.
 */
struct AdapterRxCopy {
    uint64_t Sequence;
    bool     Retained;
};

/**
 * @brief This structure contains RX-specific state: how many receive buffers we try to
 * keep posted to the driver, how many pool slots consumers may hold at once, the copy
 * slots used once that limit is reached, and receive statistics.
 */
struct AdapterRx {
    uint32_t Target;
    uint32_t RetentionLimit;
    uint32_t PoolRetained;
    uint64_t RetryAt;
    uint32_t Failures;
    
    // Once consumers hold as many pool slots as they are allowed to, frames
    // are copied here so the RX slot can go back to the driver.
    struct {
        uint32_t              Count;
        uint32_t              Retained;
        struct AdapterRxCopy* Entries;
        unsigned char*        Bytes;
    } Copy;
    
    uint64_t FallbackCopies;
    uint64_t Dropped;
};

/**
 * @brief This structure contains the batch window: the batches that have been sent to
 * the driver but not yet retired, plus counters showing how far they have progressed.
 * Batch IDs are handed out in order (NextBatch) and the driver accepts (admits) them
 * strictly in that order, so Admitted is the highest batch ID accepted so far.
 * Retired is the highest batch ID the driver has confirmed it is completely finished
 * with; batches up to that ID no longer need to be kept for resending, so their
 * window slots are freed. Highest is the highest completion sequence number the
 * driver has reported, which shows whether completions may be missing locally.
 */
struct AdapterWindow {
    struct AdapterBatch Batches[NET_ADAPTER_WINDOW_MAX];
    uint32_t            Size;
    uint32_t            BatchSize;
    uint64_t            Run;
    uint64_t            NextBatch;
    uint64_t            Admitted;
    uint64_t            Retired;
    uint64_t            Highest;
    bool                PreferRx;
};

/**
 * @brief This structure contains the control request state. At most one control
 * request is in flight.
 */
struct AdapterControl {
    NetAdapterRequest_t Request;
    NetAdapterRequest_t Output; // scratch request for ACK and drain messages
    uint64_t            NextSerial;
    uint64_t            Deadline;
    uint32_t            Attempts;
    bool                Pending;
};

/**
 * @brief This structure contains the last acknowledgement (ACK) sent to the driver and
 * when to resend it. An ACK tells the driver up to which batch and completion we have
 * processed its results, so it can free the matching resources on its side.
 */
struct AdapterAck {
    struct ctt_netadapter_ack Sent;
    uint64_t                  Deadline;
    uint32_t                  Attempts;
};

/**
 * @brief This structure contains the state of a completion drain. A drain asks the driver
 * to send again the completions after sequence number After; it is used when some
 * completion messages may have been lost. The driver answers with one or more chunks
 * of completions and a separate end message saying up to which sequence number
 * (Through) the drain goes. The end message may arrive before or after the chunks,
 * so both are tracked separately.
 */
struct AdapterDrain {
    uint64_t Id;
    uint64_t After;
    uint64_t Through;
    uint64_t SeenThrough;
    uint64_t Highest;
    uint64_t Deadline;
    uint64_t NextPoll;
    uint32_t Attempts;
    bool     Active;
    bool     Ended;
    bool     Needed;
};

/**
 * @brief This structure contains requests made by the caller (stop, close, refresh) that
 * have not been carried out yet. API calls only set these flags and return right
 * away; the request pump acts on them later, so no API call ever blocks.
 */
struct AdapterIntent {
    uint64_t StopBarrier;
    bool     StopRequested;
    bool     StopReplied;
    bool     CloseRequested;
    bool     LinkNeeded;
    bool     CountersNeeded;
};

/**
 * @brief This structure contains the complete worker state of a single adapter port.
 */
struct NetworkAdapter {
    uuid_t                Device;
    uuid_t                Driver;
    uint32_t              Port;
    uint32_t              Mtu;
    NetAdapterConfig_t    Config;
    NetAdapterCallbacks_t Callbacks;
    enum NetAdapterState  State;
    oserr_t               LastError;

    // Protocol related structures
    struct ctt_netadapter_info     Info;
    struct ctt_netadapter_session  Session;
    struct ctt_netadapter_link     Link;
    struct ctt_netadapter_counters Counters;

    // Shared packet storage and per-slot bookkeeping. It is kept until the driver
    // has confirmed that the session is closed.
    NetBufferManager_t* Buffers;

    // This is indexed by the direction (NET_ADAPTER_TX/NET_ADAPTER_RX).
    struct AdapterQueue Queues[2];
    struct AdapterTx    Tx;
    struct AdapterRx    Rx;

    // Batches sent to the driver that it has not yet confirmed it is finished with.
    struct AdapterWindow  Window;
    // The one control request (lifecycle step or query) that may be outstanding at a time.
    struct AdapterControl Control;
    // The last acknowledgement sent to the driver and when to resend it.
    struct AdapterAck     Ack;
    // Used to ask the driver to resend completions the worker may have missed.
    struct AdapterDrain   Drain;
    // Caller requests that the request pump still has to act on.
    struct AdapterIntent  Intent;
};

/**
 * @brief This structure contains the registry entry of a network adapter in the manager.
 */
struct AdapterEntry {
    NetworkAdapter_t*             Adapter;
    uuid_t                        Device;
    uuid_t                        Driver;
    uint32_t                      Port;
    int                           Endpoint;
    uint64_t                      Serial;

    // Number of messages sent. Gracht message IDs are 32-bit and must not be reused,
    // so normal traffic stops shortly before the limit to leave IDs for closing.
    uint32_t                      SentFrames;

    gracht_client_t*              Client;
    struct vali_link_message      Context;
    bool                          AwaitReply;
    uint64_t                      Now;
    uint8_t                       Operation;
    struct ctt_netadapter_session Session;
    bool                          Removed;

    // Driver most recently announced for this port. It is kept while the current
    // session is being closed, so the port can be attached to it afterwards.
    uuid_t                        PendingDriver;

    // Backoff for local allocation failures.
    uint64_t                      AttachAfter;
    enum NetAdapterState          LastState;
};

/** 
 * @brief Register a port. If the port is already registered with a different driver,
 * its current session is closed first. Requires the registry lock. Registering the
 * same port twice does not create a second session.
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
 * @brief Mark every port of a device as removed. The entries are kept until their
 * sessions have been closed by the driver.
 * @param device The unique identifier of the network device.
 */
__EXTERN void
NetAdapterRegistryRemove(
    _In_ uuid_t device);

/** 
 * @brief Find the entry whose dedicated client received a generated callback. Must be
 * called with the registry lock held.
 * @param client The client associated with the adapter entry.
 * @return The adapter entry corresponding to the client, or NULL if not found.
 */
__EXTERN struct AdapterEntry*
NetAdapterRegistryFindClient(
    _In_ gracht_client_t* client);

/** 
 * @brief Create an adapter client and register its descriptor with the I/O set.
 * On success the caller manages the client and must destroy it; on failure all
 * resources are released.
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
 * @brief Remove the client from the I/O set, then shut it down.
 * @param eventSet The I/O set the client was registered with.
 * @param client The client to destroy.
 */
__EXTERN void
NetAdapterClientDestroy(
    _In_ int              eventSet,
    _In_ gracht_client_t* client);

/** 
 * @brief Process incoming messages for one port and send any requests that are ready.
 * @param entry The adapter entry to poll.
 * @param now The current time for scheduling purposes.
 * @return True if the per-poll work limit (NET_ADAPTER_WORK_BUDGET) was reached, so
 * more work may be waiting; false otherwise.
 */
__EXTERN bool
NetAdapterTransportPoll(
    _In_ struct AdapterEntry* entry,
    _In_ uint64_t             now);

#endif //!__NETD_ADAPTER_PRIVATE_H__

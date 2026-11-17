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
 * Shared only among the files inside netd/adapters. This is not a public API.
 *
 * adapters.c manages the global list of adapters under g_lock. Transport and
 * adapter callbacks already run with that lock held, so they must never try to
 * acquire it again. Device discovery and removal arrive through the public
 * NetworkAdaptersDiscover and NetworkAdaptersRemove functions, which lock it
 * themselves.
 *
 * Each port creates its own Gracht IPC client. Knowing which client received
 * a message tells us which local port session it belongs to, while verifying
 * that the remote peer can be trusted is left to libgracht.
 */

#ifndef __NETD_ADAPTER_PRIVATE_H__
#define __NETD_ADAPTER_PRIVATE_H__

#include "adapters.h"
#include <gracht/link/vali.h>

// Maximum number of adapter ports the registry can hold at once.
#define NET_ADAPTER_LIMIT 16

// Max incoming messages or outgoing requests handled per port on one worker pass.
#define NET_ADAPTER_WORK_BUDGET 16

// Default payload MTU in bytes when NET_ADAPTER_MTU_AUTO is configured.
#define NET_ADAPTER_MTU_DEFAULT 1500

// Delay in milliseconds before retrying attachment to a discovered device.
#define NET_ADAPTER_ATTACH_RETRY_MILLISECONDS 1000

// Longest time in milliseconds the worker sleeps before checking retry timers.
#define NET_ADAPTER_WORKER_WAIT_MILLISECONDS 10

/**
 * @brief Saved copy of a single TX or RX batch sent to the driver.
 *
 * Keeping the full request lets the scheduler resend the exact same batch if
 * no admission reply arrives. After the driver admits the batch, this entry
 * stays alive until the driver confirms it has retired it completely. That way
 * a late duplicate admission is checked against Results and cannot affect a
 * newer packet reusing the same slot.
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
 * @brief Lease lookup table for one transfer direction (NET_ADAPTER_TX or NET_ADAPTER_RX).
 */
struct AdapterQueue {
    struct AdapterLease* Leases;
    uint32_t             Slots;
};

/**
 * @brief Transmit-specific state. QueueOrder assigns a rising sequence number to
 * each submitted frame so packets leave oldest first even as pool slots reuse
 * out of order.
 */
struct AdapterTx {
    uint64_t QueueOrder;
};

/**
 * @brief State of one backup RX copy buffer.
 * Copy buffers are allocated once per session. Sequence rises on every reuse and
 * stops at UINT64_MAX (never wrapping) so a stale release handle cannot free a
 * newer packet in the same slot.
 */
struct AdapterRxCopy {
    uint64_t Sequence;
    bool     Retained;
};

/**
 * @brief Receive-side limits, backup copy buffers, and statistics.
 */
struct AdapterRx {
    uint32_t Target;         // Desired number of RX buffers kept posted to the driver.
    uint32_t RetentionLimit; // Max RX pool slots the consumer may hold at once.
    uint32_t PoolRetained;   // RX pool slots currently held by the consumer.
    uint64_t RetryAt;        // Earliest monotonic timestamp (ms) to post new RX batches after a rejection.
    uint32_t Failures;       // Consecutive RX batches where the driver rejected every buffer.
    
    // Backup copy buffers allocated up front. Used once the consumer holds
    // RetentionLimit pool slots so the original pool slot returns to the driver.
    struct {
        uint32_t              Count;
        uint32_t              Retained;
        struct AdapterRxCopy* Entries;
        unsigned char*        Bytes;
    } Copy;
    
    uint64_t FallbackCopies; // Frames copied to backup buffers.
    uint64_t Dropped;        // Frames dropped because no receive callback was set.
};

/**
 * @brief Tracks batches sent to the driver and how far the driver has progressed.
 *
 * Batch IDs increase one by one from NextBatch, and the driver admits them in
 * order. Admitted tracks the highest gap-free batch ID the driver accepted.
 * Retired tracks the highest batch ID the driver has completely finished with,
 * freeing those window slots for reuse. Highest records the newest completion
 * sequence reported by the driver so gaps can trigger a drain.
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
 * @brief Tracks the single control request allowed in flight, plus a scratch
 * buffer for building one-way ACK and drain requests.
 */
struct AdapterControl {
    NetAdapterRequest_t Request;
    NetAdapterRequest_t Output; // Scratch buffer for one-way ACK and drain requests.
    uint64_t            NextSerial;
    uint64_t            Deadline;
    uint32_t            Attempts;
    bool                Pending;
};

/**
 * @brief Tracks the last acknowledgement sent to the driver and its retry timer.
 * Sending an ACK lets the driver drop its saved copies of batches and packet
 * completions we have already handled.
 */
struct AdapterAck {
    struct ctt_netadapter_ack Sent;
    uint64_t                  Deadline;
    uint32_t                  Attempts;
};

/**
 * @brief Tracks an active completion drain request.
 *
 * A drain asks the driver to resend completion records after sequence After
 * when completions appear to be missing. The driver replies with one or more
 * completion chunks plus an end marker giving the final sequence (Through).
 * Because the end marker can arrive ahead of or behind the chunks, both are
 * tracked separately.
 */
struct AdapterDrain {
    uint64_t Id;          // Request ID of the active or last drain.
    uint64_t After;       // Ask for completions strictly newer than this sequence.
    uint64_t Through;     // Final sequence covered by this drain, from the end marker.
    uint64_t SeenThrough; // Highest sequence seen in completion chunks for this drain.
    uint64_t Highest;     // Driver's newest completion sequence when the drain ended.
    uint64_t Deadline;    // Monotonic timestamp (ms) when this drain times out and retries.
    uint64_t NextPoll;    // Earliest time (ms) for the next periodic poll while packets are active.
    uint32_t Attempts;    // Retry counter for the current drain.
    bool     Active;      // True while waiting for chunks and the end marker.
    bool     Ended;       // True once the end marker arrived.
    bool     Needed;      // True when missing completions or stop logic require a new drain.
};

/**
 * @brief Caller requests recorded without blocking so the scheduler can act on
 * them on its next turn.
 */
struct AdapterIntent {
    uint64_t StopBarrier;    // Last completion sequence the driver will emit for the stopping run.
    bool     StopRequested;  // Caller asked to stop the current run.
    bool     StopReplied;    // Driver replied to STOP_RUN with StopBarrier.
    bool     CloseRequested; // Caller or transport asked to close the session.
    bool     LinkNeeded;     // Need to query fresh link state after a drain or event.
    bool     CountersNeeded; // Caller asked for fresh hardware traffic counters.
};

/**
 * @brief Full worker-side state for one network adapter port session.
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

    // Driver capabilities, session identity, link state, and cached counters.
    struct ctt_netadapter_info     Info;
    struct ctt_netadapter_session  Session;
    struct ctt_netadapter_link     Link;
    struct ctt_netadapter_counters Counters;

    // Shared packet memory and slot tracking, kept until the driver confirms close.
    NetBufferManager_t* Buffers;

    // Indexed by direction (NET_ADAPTER_TX or NET_ADAPTER_RX).
    struct AdapterQueue Queues[2];
    struct AdapterTx    Tx;
    struct AdapterRx    Rx;

    struct AdapterWindow  Window;  // Active TX/RX batches waiting to be retired.
    struct AdapterControl Control; // Single pending control request, if any.
    struct AdapterAck     Ack;     // Last sent ACK and its retry timer.
    struct AdapterDrain   Drain;   // Completion recovery drain state.
    struct AdapterIntent  Intent;  // Deferred caller requests for the scheduler.
};

/**
 * @brief One slot in the global port registry managed by adapters.c.
 */
struct AdapterEntry {
    NetworkAdapter_t*             Adapter;
    uint64_t                      InstanceId;
    uuid_t                        Device;
    uuid_t                        Driver;
    uint32_t                      Port;
    int                           Endpoint;
    uint64_t                      Serial;

    // Count of Gracht messages sent on this client. Gracht uses 32-bit message
    // IDs that must not wrap while replies may still arrive, so normal traffic
    // closes the session shortly before UINT32_MAX to leave room for shutdown.
    uint32_t                      SentFrames;

    gracht_client_t*              Client;
    struct vali_link_message      Context;
    bool                          AwaitReply;
    uint64_t                      Now;
    uint8_t                       Operation;
    struct ctt_netadapter_session Session;
    bool                          Removed;

    // Driver waiting to attach once the previous session finishes closing.
    uuid_t                        PendingDriver;

    // Earliest monotonic timestamp (ms) before retrying a failed local attach.
    uint64_t                      AttachAfter;
    enum NetAdapterState          LastState;
};

/** 
 * @brief Register a discovered port in the registry.
 * Must be called with the registry lock held. If the port already has a
 * different driver attached, that session is closed first. Registering the same
 * device, driver, and port twice does not create a duplicate session.
 *
 * @param device Network device UUID.
 * @param driver Network driver UUID.
 * @param port Port index on the device.
 */
__EXTERN void
NetAdapterRegistryDiscover(
    _In_ uuid_t   device,
    _In_ uuid_t   driver,
    _In_ uint32_t port);

/** 
 * @brief Mark every port belonging to a device as removed.
 * Entries stay in the registry until their driver closes the session and
 * callers return all held packet handles.
 *
 * @param device Network device UUID.
 */
__EXTERN void
NetAdapterRegistryRemove(
    _In_ uuid_t device);

/** 
 * @brief Look up the registry entry attached to a given Gracht client.
 * Must be called with the registry lock held.
 *
 * @param client Gracht client pointer from an incoming callback.
 * @return Matching registry entry, or null if none is active.
 */
__EXTERN struct AdapterEntry*
NetAdapterRegistryFindClient(
    _In_ gracht_client_t* client);

/** 
 * @brief Create a dedicated Gracht client for one port and add it to the I/O set.
 * On success, the caller must later destroy the client with NetAdapterClientDestroy.
 * On failure, all partially created resources are cleaned up.
 *
 * @param eventSet I/O set descriptor polled by the worker thread.
 * @param protocol Gracht protocol descriptor to register on the client.
 * @param clientOut Receives the connected Gracht client on success.
 * @return 0 on success, or a negative errno-style error code on failure.
 */
__EXTERN int
NetAdapterClientCreate(
    _In_  int                eventSet,
    _In_  gracht_protocol_t* protocol,
    _Out_ gracht_client_t**  clientOut);

/** 
 * @brief Remove a port's Gracht client from the I/O set and shut it down.
 * @param eventSet I/O set descriptor where the client was registered.
 * @param client Client to destroy. Ignored if null.
 */
__EXTERN void
NetAdapterClientDestroy(
    _In_ int              eventSet,
    _In_ gracht_client_t* client);

/** 
 * @brief Drain incoming messages for one port and send any due requests.
 * @param entry Registry entry to poll.
 * @param now Current monotonic time in milliseconds.
 * @return true if the per-poll limit (NET_ADAPTER_WORK_BUDGET) was hit and more
 *         work may still be waiting, false otherwise.
 */
__EXTERN bool
NetAdapterTransportPoll(
    _In_ struct AdapterEntry* entry,
    _In_ uint64_t             now);

#endif //!__NETD_ADAPTER_PRIVATE_H__

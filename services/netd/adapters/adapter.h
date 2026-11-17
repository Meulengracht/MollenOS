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
 * Core API for a single netd adapter session.
 *
 * This module has no dependency on the IPC transport itself. That keeps it
 * easy to drive either from the Gracht client loop or directly from host tests.
 *
 * How the files fit together:
 *   adapter.c   - Session startup, capability checks, state changes, and replies.
 *   buffers.c   - Shared-memory packet pools and per-slot tracking.
 *   tx.c        - Building and queuing outgoing (TX) frames.
 *   rx.c        - Delivering incoming (RX) frames and managing backup copy slots.
 *   queue.c     - Sizing pools and packing TX or RX leases into driver batches.
 *   scheduler.c - Picking the next protocol request or retry to send.
 *   events.c    - Checking and applying unsolicited driver events.
 *   requests.c  - Sending scheduled requests over Gracht and polling for replies.
 *   client.c    - Creating per-port Gracht clients and copying incoming events.
 *   adapters.c  - Global port registry, lock, and single worker thread.
 *
 * Each adapter runs on one worker thread, one call at a time. On each turn, the
 * scheduler (NetAdapterNextRequest) returns at most one request to send. Control
 * requests wait for a matching NetAdapterReply_t. Unsolicited updates from the
 * driver, such as batch admissions, acknowledgements, and packet completions,
 * arrive as NetAdapterEvent_t values.
 *
 * When a request times out, the exact same request or batch is sent again with
 * its original ID. That way the driver can spot duplicates instead of running
 * the same work twice if the first copy already arrived.
 *
 * Callbacks run directly inside the worker call, so they must return quickly
 * and must never call back into this API. Driver and session ID checks filter
 * out stale messages from earlier drivers. Real peer authentication lives in
 * the IPC layer, not here.
 */

#ifndef __NETMANAGER_ADAPTER_H__
#define __NETMANAGER_ADAPTER_H__

#include "buffers.h"

#define NET_ADAPTER_BATCH_MAX CTT_NETADAPTER_LIMIT_BATCH_RECORDS

// Maximum number of batches that may be sent but not yet retired.
#define NET_ADAPTER_WINDOW_MAX 8

// Default number of shared TX packet slots requested for each adapter.
#define NET_ADAPTER_TX_SLOTS_DEFAULT 32

// Default number of shared RX packet slots requested for each adapter.
#define NET_ADAPTER_RX_SLOTS_DEFAULT 32

// Zero selects the negotiated MTU instead of requesting a specific value.
#define NET_ADAPTER_MTU_AUTO 0

// Default budget for packet buffers and adapter bookkeeping: one MiB.
#define NET_ADAPTER_MEMORY_BUDGET_DEFAULT (1024ULL * 1024ULL)

// Default delay before retrying an unanswered request, in milliseconds.
#define NET_ADAPTER_RETRY_MILLISECONDS_DEFAULT 1000

// Default maximum number of request sends, including the first send.
#define NET_ADAPTER_RETRY_LIMIT_DEFAULT 3

// Default interval between asynchronous status polls, in milliseconds.
#define NET_ADAPTER_POLL_MILLISECONDS_DEFAULT 100

// Default maximum number of RX pool slots consumers may retain at once.
#define NET_ADAPTER_RX_RETAINED_SLOTS_DEFAULT 8

// Default number of preallocated RX copy slots.
#define NET_ADAPTER_RX_COPY_SLOTS_DEFAULT 8

// Default number of batches allowed in flight, bounded by NET_ADAPTER_WINDOW_MAX.
#define NET_ADAPTER_BATCH_WINDOW_DEFAULT 4

// Forward declarations
typedef struct NetworkAdapter NetworkAdapter_t;

/**
 * @brief Lifecycle states for a single network adapter port.
 *
 * Normal startup walks through these states in order:
 *   INFO -> OPEN -> REGISTER_TX -> REGISTER_RX -> CONFIGURE -> PREPARE -> PRIME -> START -> RUNNING
 *
 * Stopping a run pauses traffic without tearing down shared memory:
 *   RUNNING (or PRIME) -> STOPPING -> STOPPED
 * From STOPPED, NetAdapterStart moves back to PREPARE for a new traffic run.
 *
 * Closing tears down the session cleanly:
 *   CLOSING -> CLOSED
 *
 * Error states:
 *   FAILED      - Unrecoverable error where no driver session or shared buffer
 *                 is left active (or after a close completed on failure).
 *   QUARANTINED - An OPEN or CLOSE outcome is unknown, so the driver may still
 *                 access shared memory. Buffers stay allocated until a retry
 *                 resolves the request.
 */
enum NetAdapterState {
    NET_ADAPTER_INFO,
    NET_ADAPTER_OPEN,
    NET_ADAPTER_REGISTER_TX,
    NET_ADAPTER_REGISTER_RX,
    NET_ADAPTER_CONFIGURE,
    NET_ADAPTER_PREPARE,
    NET_ADAPTER_PRIME,
    NET_ADAPTER_START,
    NET_ADAPTER_RUNNING,
    NET_ADAPTER_STOPPING,
    NET_ADAPTER_STOPPED,
    NET_ADAPTER_CLOSING,
    NET_ADAPTER_CLOSED,
    NET_ADAPTER_FAILED,
    NET_ADAPTER_QUARANTINED
};

/**
 * @brief Resource limits and timers used when opening and running a port.
 *
 * MTU is the largest packet payload in bytes, excluding the 14-byte Ethernet
 * header (NET_ADAPTER_ETHERNET_HEADER_SIZE). Passing NET_ADAPTER_MTU_AUTO picks
 * the smaller of the driver's current MTU and NET_ADAPTER_MTU_DEFAULT, raised to
 * the driver's minimum MTU if needed.
 *
 * TxSlots, RxSlots, and BatchWindow are upper bounds. If the driver advertises
 * smaller limits, the driver's limits win.
 */
typedef struct NetAdapterConfig {
    uint32_t TxSlots;            // Number of TX packet buffers to request. Must be nonzero.
    uint32_t RxSlots;            // Number of RX packet buffers to request. Must be nonzero and meet the driver minimum.
    uint32_t Mtu;                // Payload MTU in bytes, or NET_ADAPTER_MTU_AUTO to use the negotiated default.
    uint64_t MemoryBudget;       // Total byte cap for shared packet buffers and adapter tables. Must be nonzero.
    uint32_t RetryMilliseconds;  // Wait time in milliseconds before resending an unanswered request. Must be nonzero.
    uint32_t RetryLimit;         // Total send attempts per request before giving up. Must be nonzero.
    uint32_t PollMilliseconds;   // Interval in milliseconds between status polls while packets are active. Must be nonzero.
    uint32_t RxRetainedSlots;    // Max RX pool buffers the consumer may hold at once. Zero forces immediate copy or lending.
    uint32_t RxCopySlots;        // Backup RX copy buffers allocated at startup for when RxRetainedSlots is full. Zero disables copies.
    uint32_t BatchWindow;        // Max batches allowed with the driver at once (1..NET_ADAPTER_WINDOW_MAX, capped by driver limit).
} NetAdapterConfig_t;

/** 
 * @brief Identifies which kind of memory backs a packet handle inside netd.
 * This tag is internal to netd and is never sent to the driver.
 *   - NONE: Cleared or invalid packet.
 *   - POOL: Slot inside the shared-memory pool registered with the driver.
 *   - COPY: Backup RX buffer allocated in netd's private memory.
 */
enum NetAdapterPacketBacking {
    NET_ADAPTER_PACKET_NONE,
    NET_ADAPTER_PACKET_POOL,
    NET_ADAPTER_PACKET_COPY
};

/**
 * @brief Internal handle linking a TX or RX packet back to its underlying slot.
 * Callers outside the adapter core must treat every field, including Owner, as opaque.
 */
typedef struct NetAdapterPacketIdentity {
    NetworkAdapter_t*            Owner;
    enum NetAdapterPacketBacking Backing;
    union {
        NetBufferLease_t Pool;
        struct {
            uint32_t Slot;
            uint64_t Sequence;
        } Copy;
    } Storage;
} NetAdapterPacketIdentity_t;

/** 
 * @brief Read-only received packet offered through ReceivePacket.
 *
 * Packets are not reference counted. Only one piece of code may hold a packet at
 * a time. If ReceivePacket returns true, copy this struct once into your queue
 * and later pass it once to NetAdapterRxRelease. Data stays readable even if the
 * adapter stops or closes before release. Treat Private as opaque.
 */
typedef struct NetAdapterRxPacket {
    const void*                Data;
    uint32_t                   Length;
    NetAdapterPacketIdentity_t Private;
} NetAdapterRxPacket_t;

/**
 * @brief Return a packet accepted from ReceivePacket back to the adapter.
 *
 * Call this from the adapter's worker context, never from inside a receive
 * callback. To decline a packet, return false from ReceivePacket instead. Code
 * running outside the worker should call NetworkAdaptersRxRelease, which takes
 * the registry lock first.
 *
 * @param adapter Adapter that produced the packet.
 * @param packet Accepted packet handle. Cleared on success.
 * @return OS_EOK when released, OS_EINVALPARAMS if an argument is null, or
 *         OS_ENOENT if the handle is stale, already released, or belongs to
 *         another adapter.
 */
__EXTERN oserr_t
NetAdapterRxRelease(
    _In_ NetworkAdapter_t*     adapter,
    _In_ NetAdapterRxPacket_t* packet);

/**
 * @brief Summary of an adapter's link-layer readiness for network consumers.
 * Mtu is the payload limit (excluding the Ethernet header) and stays zero until
 * the driver reports its capabilities.
 */
typedef struct NetAdapterStatus {
    enum NetAdapterState State;
    uint32_t             Mtu;
    uint8_t              Mac[6];
} NetAdapterStatus_t;

/**
 * @brief Event callbacks invoked synchronously on the adapter worker.
 *
 * Callbacks must never block or call back into this API.
 *
 * Transmitted fires once for every send accepted by NetAdapterTxSubmit or
 * NetAdapterSend, whether it succeeds, fails, or gets cancelled. It does not
 * fire if the submit call itself fails. A successful status means the network
 * hardware finished reading the frame from shared memory (DMA), not that a
 * remote peer received it.
 *
 * Link only fires when the driver sends a validated report with a higher
 * sequence number than the previous one.
 */
typedef struct NetAdapterCallbacks {
    /**
     * @brief Deliver a received frame when ReceivePacket is unset or declines it.
     * The frame pointer is only valid until this callback returns.
     * @param context User-defined pointer passed to all callbacks.
     * @param frame Start of the received Ethernet frame.
     * @param length Frame size in bytes.
     */
    void (*Receive)(void* context, const void* frame, uint32_t length);

    /**
     * @brief Report the outcome of a queued transmit frame.
     * @param context User-defined pointer passed to all callbacks.
     * @param cookie Caller value supplied when the frame was submitted.
     * @param status OS_EOK when hardware finished reading the frame, or an error/cancellation code.
     */
    void (*Transmitted)(void* context, uint64_t cookie, oserr_t status);

    /**
     * @brief Report a validated link state change with a newer sequence number.
     * @param context User-defined pointer passed to all callbacks.
     * @param link Carrier, duplex, and speed state. Valid only during this call.
     */
    void (*Link)(void* context, const struct ctt_netadapter_link* link);
    
    /** 
     * @brief Offer a received packet so the consumer can keep reading it after the
     * callback returns. Return true to accept it (you must release it later with
     * NetAdapterRxRelease) or false to decline.
     *
     * Consumers may only hold RxRetainedSlots pool buffers at once. Past that cap,
     * the adapter copies the frame into a backup copy slot so the shared pool
     * buffer can go back to the driver right away. If this callback is unset,
     * declines, or all copy slots are busy, the adapter falls back to Receive, and
     * drops the frame if Receive is also unset. If you pass an accepted packet to
     * another thread, synchronize that handoff yourself.
     *
     * @param context User-defined pointer passed to all callbacks.
     * @param packet Offered packet handle.
     * @return true if accepted for later release, false to decline.
     */
    bool (*ReceivePacket)(void* context, const NetAdapterRxPacket_t* packet);

    /**
     * @brief Report every adapter state change along with the effective MTU and MAC.
     * Runs immediately when the state changes, before any packet callbacks for the
     * new state.
     * @param context User-defined pointer passed to all callbacks.
     * @param status Current adapter status. Valid only during this call.
     */
    void (*State)(void* context, const NetAdapterStatus_t* status);

    // Opaque pointer passed back as context in every callback.
    void* Context;
} NetAdapterCallbacks_t;

/**
 * @brief Outgoing protocol request built by NetAdapterNextRequest for the IPC layer.
 *
 * Only the fields needed by Operation are populated. Serial matches a control
 * request to its reply and stays unchanged on retries. The returned pointer is
 * only valid until the next call into the adapter core.
 *
 * When a TX or RX batch is resent, its batch ID (Value), Run, and packet list
 * stay identical so the driver recognizes it as a repeat. Ack may carry newer
 * numbers because netd can always acknowledge more completed work.
 */
typedef struct NetAdapterRequest {
    uint64_t                      Serial;
    uint8_t                       Operation;
    uuid_t                        Device;
    uuid_t                        Driver;
    uint32_t                      Port;
    uint32_t                      Mtu;
    uint32_t                      Count;
    struct ctt_netadapter_session Session;
    // Pool registration ID, batch ID, drain ID, or the last admitted batch ID for START_RUN/STOP_RUN.
    uint64_t                      Value;
    uint64_t                      Run;
    uint64_t                      After;
    struct ctt_netadapter_ack    Ack;
    struct ctt_netadapter_pool   Pool;
    struct ctt_netadapter_packet Packets[NET_ADAPTER_BATCH_MAX];
} NetAdapterRequest_t;

/**
 * @brief Decoded reply to a control request, passed to HandleAdapterRequest.
 * Only the fields defined for the matching request operation are meaningful.
 */
typedef struct NetAdapterReply {
    oserr_t                        Status;
    struct ctt_netadapter_info     Info;
    struct ctt_netadapter_session  Session;
    struct ctt_netadapter_link     Link;
    struct ctt_netadapter_counters Counters;
    // Enabled feature mask (OPEN), last completion sequence for STOP_RUN, or highest sequence for a drain.
    uint64_t                       Value;
    uint32_t                       PoolId;
} NetAdapterReply_t;

/**
 * @brief Nonblocking copy of an adapter's cached state, link, counters, and buffer usage.
 * Buffer fields remain zero until shared memory pools are created.
 */
typedef struct NetAdapterSnapshot {
    enum NetAdapterState           State;
    // Effective payload MTU negotiated at open (may differ from Info.current_mtu). Zero before capabilities arrive.
    uint32_t                       Mtu;
    // Unique session instance ID filled in by the registry in adapters.c.
    uint64_t                       InstanceId;
    oserr_t                        LastError;
    struct ctt_netadapter_info     Info;
    struct ctt_netadapter_link     Link;
    struct ctt_netadapter_counters Counters;
    NetBufferStats_t               Buffers;
    uint64_t                       Run;               // Current start/stop run number.
    uint64_t                       AdmittedBatch;     // Highest batch ID accepted by the driver without gaps.
    uint64_t                       RetiredBatch;      // Highest batch ID the driver has finished with completely.
    uint32_t                       PendingBatches;    // Batches currently sitting in the active window.
    uint32_t                       RxPoolRetained;    // RX pool slots currently held by the consumer.
    uint32_t                       RxCopyRetained;    // RX copy slots currently held by the consumer.
    uint64_t                       RxFallbackCopies;  // Total frames copied to backup slots because RxRetainedSlots was full.
    uint64_t                       RxDropped;         // Total frames dropped because no receive callback was installed.
} NetAdapterSnapshot_t;

/**
 * @brief Copy of an unsolicited driver event produced by the transport callbacks.
 *
 * Generated decoder arrays only live for the duration of the IPC callback, so
 * client.c checks Count against NET_ADAPTER_BATCH_MAX and copies the entries
 * here before calling NetAdapterEvent. For batch admission events, Id is the
 * batch ID. For completion and drain-end events, Id is the drain request ID
 * (or zero for unsolicited completions).
 */
typedef struct NetAdapterEvent {
    uint8_t                          Operation;
    struct ctt_netadapter_session    Session;
    uint64_t                         Run;
    uint64_t                         Id;
    uint64_t                         After;
    uint64_t                         Through;
    uint64_t                         Highest;
    oserr_t                          Status;
    uint32_t                         Count;
    struct ctt_netadapter_ack        Ack;
    struct ctt_netadapter_progress   Progress;
    struct ctt_netadapter_admission  Admissions[NET_ADAPTER_BATCH_MAX];
    struct ctt_netadapter_completion Completions[NET_ADAPTER_BATCH_MAX];
} NetAdapterEvent_t;

/** 
 * @brief Writable transmit packet backed by a shared-memory pool slot.
 *
 * Packets are not reference counted. Do not copy this struct or access it from
 * multiple threads at once. Data points to the start of the Ethernet header, and
 * Capacity covers the writable frame area (excluding any private device headroom
 * or tailroom). Data remains valid until a successful NetAdapterTxSubmit or
 * NetAdapterTxCancel clears the struct. Treat Private as opaque.
 */
typedef struct NetAdapterTxPacket {
    void*                      Data;
    uint32_t                   Capacity;
    NetAdapterPacketIdentity_t Private;
} NetAdapterTxPacket_t;

/**
 * @brief Populate a configuration struct with safe default limits and timers.
 * RX limits are automatically lowered later if the driver supports less.
 *
 * @param config Configuration struct to fill. Ignored if null.
 */
__EXTERN void
NetAdapterConfigInitializeDefault(
    _In_ NetAdapterConfig_t* config);

/** 
 * @brief Allocate a new adapter in its initial state and copy its configuration
 * and optional callbacks. No IPC is sent here. The scheduler starts by asking
 * the driver for its capabilities on the first poll.
 *
 * @param device Network device UUID.
 * @param driver Driver endpoint UUID used to send requests and validate replies.
 * @param port Port index on the device.
 * @param config Configuration limits and timers to copy.
 * @param callbacks Optional callback table to copy, or null for none.
 * @param out Receives the new adapter, or null on failure.
 * @return OS_EOK on success, OS_EINVALPARAMS for bad arguments or config, or
 *         OS_EOOM if allocation fails.
 */
__EXTERN oserr_t
NetAdapterCreate(
    _In_  uuid_t                       device,
    _In_  uuid_t                       driver,
    _In_  uint32_t                     port,
    _In_  const NetAdapterConfig_t*    config,
    _In_  const NetAdapterCallbacks_t* callbacks,
    _Out_ NetworkAdapter_t**           out);

/** 
 * @brief Replace the adapter's callback table.
 * Must be called from the adapter's single worker context. Passing null clears
 * all callbacks. This only changes future notifications. Any received packets
 * already accepted through ReceivePacket stay with the consumer until released.
 * 
 * @param adapter Adapter to update. Ignored if null.
 * @param callbacks New callback table, or null to clear all callbacks.
 */
__EXTERN void
NetAdapterSetCallbacks(
    _In_ NetworkAdapter_t*            adapter, 
    _In_ const NetAdapterCallbacks_t* callbacks);

/** 
 * @brief Free an adapter and its local resources once all work has finished.
 *
 * The adapter must already be CLOSED or FAILED, and callers must have returned
 * every TX and RX packet handle they held. Destroy refuses to run while an OPEN
 * or CLOSE outcome is still unknown, because the driver might still have an
 * active session and access the shared memory via DMA.
 * 
 * @param adapter Address of the adapter pointer. Passing a null pointer is
 *                invalid, while *adapter == NULL returns OS_EOK immediately.
 * @return OS_EOK and sets *adapter to null on success, OS_EINVALPARAMS if
 *         adapter is null, OS_EBUSY while the adapter is active or packets are
 *         still held, or an error from tearing down the buffer pools.
 */
__EXTERN oserr_t
NetAdapterDestroy(
    _In_ NetworkAdapter_t** adapter);

/**
 * @brief Apply a decoded control reply to the matching pending request.
 *
 * The reply is only accepted if it comes from the adapter's driver while a
 * control request with the same serial is pending. That blocks stale or
 * misrouted replies from touching the session state.
 *
 * If an OPEN or CLOSE reply is malformed or fails, the adapter enters
 * QUARANTINED and keeps the saved request so a retry can resend it with the
 * same identity. That avoids leaking a session the driver might have created.
 * 
 * @param adapter Adapter waiting for the reply.
 * @param driver Source driver UUID.
 * @param serial Request serial number returned by the transport.
 * @param reply Decoded reply payload.
 * @param now Current monotonic time in milliseconds, used to schedule follow-up timers.
 * @return OS_EOK if applied, OS_ENOENT if no matching request is pending,
 *         OS_EINVALPARAMS for null pointers, OS_EPROTOCOL for a bad reply,
 *         or the error returned by the driver or local setup.
 */
__EXTERN oserr_t
HandleAdapterRequest(
    _In_ NetworkAdapter_t*        adapter,
    _In_ uuid_t                   driver,
    _In_ uint64_t                 serial,
    _In_ const NetAdapterReply_t* reply,
    _In_ uint64_t                 now);

/**
 * @brief Check whether a request expects a synchronous control reply.
 * Batch posts, acknowledgements, and drains report results later through events
 * instead of control replies.
 *
 * @param request Request returned by NetAdapterNextRequest. Null returns false.
 * @return true for a control request, false for a one-way request or null.
 */
__EXTERN bool
NetAdapterRequestIsControl(
    _In_ const NetAdapterRequest_t* request);

/**
 * @brief Return the next request the transport should send, if one is ready.
 * The returned pointer points to internal storage and stays valid only until the
 * next call into the adapter core. Retries keep the original request identity.
 *
 * @param adapter Adapter to schedule.
 * @param now Current monotonic time in milliseconds.
 * @param out Receives the request pointer, or null when nothing is due.
 * @return OS_EOK when a request is ready, OS_ENOENT when nothing is due,
 *         OS_EINVALPARAMS for null inputs, or an error (such as OS_ETIMEOUT)
 *         if scheduling failed the adapter.
 */
__EXTERN oserr_t
NetAdapterNextRequest(
    _In_  NetworkAdapter_t*           adapter,
    _In_  uint64_t                    now,
    _Out_ const NetAdapterRequest_t** out);

/**
 * @brief Validate and apply an unsolicited driver event (batch admission,
 * packet completions, drain end, or ACK progress).
 * Every record in the event is checked before any lease or callback is touched.
 * A malformed event fails the adapter.
 *
 * @param adapter Adapter receiving the event.
 * @param driver Source driver UUID.
 * @param event Decoded event copied out of temporary transport storage.
 * @param now Current monotonic time in milliseconds.
 * @return OS_EOK when applied or when missing completions trigger a drain,
 *         OS_ENOENT for stale or unrelated events, OS_EINVALPARAMS for null
 *         inputs or an unknown operation, or OS_EPROTOCOL / another error if
 *         the event was invalid.
 */
__EXTERN oserr_t
NetAdapterEvent(
    _In_ NetworkAdapter_t*        adapter,
    _In_ uuid_t                   driver,
    _In_ const NetAdapterEvent_t* event,
    _In_ uint64_t                 now);

/** 
 * @brief Reserve a TX pool slot so the caller can write a frame directly into it.
 *
 * This does not queue a transmission yet. The caller has sole access to Data
 * until NetAdapterTxSubmit or NetAdapterTxCancel is called. The adapter must be
 * running and its link must be up. Unsubmitted packets keep the pool alive across
 * stop and close, so always cancel any packet you decide not to send.
 * 
 * @param adapter Running adapter.
 * @param packet Output packet handle. Cleared on failure.
 * @return OS_EOK on success, OS_EINVALPARAMS for null inputs, OS_ENOTCONNECTED
 *         when TX is unavailable, or the error from the buffer pool.
 */
__EXTERN oserr_t
NetAdapterTxAcquire(
    _In_ NetworkAdapter_t*      adapter,
    _Out_ NetAdapterTxPacket_t* packet);

/** 
 * @brief Queue a finished Ethernet frame written into a TX slot, without copying.
 *
 * Length includes the 14-byte Ethernet header and must fall between
 * NET_ADAPTER_ETHERNET_HEADER_SIZE and MTU + NET_ADAPTER_ETHERNET_HEADER_SIZE.
 * On success, the adapter takes over the slot, clears packet, and later calls
 * Transmitted with cookie. On failure, the caller still holds packet and must
 * fix and resubmit it or cancel it.
 *
 * Packets acquired before a stop/restart cannot be sent in the new run. Cancel
 * them instead. Calls into this API must be serialized on the worker, though the
 * caller may fill Data on another thread while holding the packet.
 * 
 * @param adapter Adapter the packet came from.
 * @param packet Acquired TX packet handle. Cleared only on success.
 * @param length Complete Ethernet frame length in bytes.
 * @param cookie Caller value passed back to Transmitted.
 * @return OS_EOK on success, OS_EINVALPARAMS for bad inputs or length,
 *         OS_ENOENT for an invalid handle, OS_ENOTCONNECTED if the run ended or
 *         the link dropped, OS_EOVERFLOW if the TX order counter ran out, or a
 *         pool error.
 */
__EXTERN oserr_t
NetAdapterTxSubmit(
    _In_ NetworkAdapter_t*     adapter,
    _In_ NetAdapterTxPacket_t* packet,
    _In_ uint32_t              length,
    _In_ uint64_t              cookie);

/** 
 * @brief Return an acquired TX slot that will not be sent.
 * Works even after stop, close, or link loss. Drops the frame without calling
 * Transmitted.
 * 
 * @param adapter Adapter the packet came from.
 * @param packet Acquired TX packet handle. Cleared on success.
 * @return OS_EOK on success, OS_EINVALPARAMS for null inputs, OS_ENOENT if the
 *         handle is stale, already submitted, or belongs to another adapter, or
 *         a pool release error.
 */
__EXTERN oserr_t
NetAdapterTxCancel(
    _In_ NetworkAdapter_t*     adapter,
    _In_ NetAdapterTxPacket_t* packet);

/** 
 * @brief Copy an Ethernet frame into a TX pool slot and queue it in one step.
 * Equivalent to calling Acquire, memcpy, and Submit. Local rejection returns an
 * error directly and never calls Transmitted. Requires a running adapter and an
 * active link.
 * 
 * @param adapter Running adapter.
 * @param frame Frame bytes to copy, starting at the Ethernet header.
 * @param length Frame length in bytes.
 * @param cookie Caller value passed back to Transmitted if queued.
 * @return OS_EOK when queued, OS_EINVALPARAMS for bad inputs or length, or an
 *         error from acquire/submit such as OS_ENOTCONNECTED or OS_EBUSY.
 */
__EXTERN oserr_t
NetAdapterSend(
    _In_ NetworkAdapter_t* adapter,
    _In_ const void*       frame,
    _In_ uint32_t          length,
    _In_ uint64_t          cookie);

/** 
 * @brief Request an orderly stop of the current traffic run.
 *
 * Takes effect right away for new TX: further acquires and submits are rejected.
 * The scheduler finishes the stop in the background. It stops sending new
 * batches, waits for every batch already in flight to be answered, sends
 * STOP_RUN, and drains and acknowledges all remaining completions up to the
 * driver's stop sequence. The adapter must reach STOPPED before NetAdapterStart
 * can start a new run. Null is ignored.
 * 
 * @param adapter Adapter to stop.
 */
__EXTERN void
NetAdapterStop(
    _In_ NetworkAdapter_t* adapter);

/** 
 * @brief Start a new traffic run on a STOPPED adapter.
 * Schedules PREPARE_RUN, initial RX buffer priming, and START_RUN in the
 * background without blocking.
 * 
 * @param adapter Stopped adapter.
 * @return OS_EOK when scheduled, OS_EINVALPARAMS if null, OS_EBUSY if not in
 *         NET_ADAPTER_STOPPED, or OS_EOVERFLOW if the run counter ran out.
 */
__EXTERN oserr_t
NetAdapterStart(
    _In_ NetworkAdapter_t* adapter);

/** 
 * @brief Request an orderly close of the adapter, even during startup.
 *
 * New TX is rejected right away. If an OPEN request is still waiting for a
 * reply, the scheduler resolves it first (resending with the same serial if
 * needed) because the driver may already have opened a session. Shared buffers
 * are only freed after the driver confirms the close. Null is ignored.
 * 
 * @param adapter Adapter to close.
 */
__EXTERN void
NetAdapterClose(
    _In_ NetworkAdapter_t* adapter);

/** 
 * @brief Retry a failed or quarantined adapter.
 *
 * In QUARANTINED, the pending OPEN or CLOSE request is resent with its original
 * serial and parameters so the driver recognizes it as a repeat. In FAILED with
 * no open session or allocated pools, startup restarts from GET_INFO. Retrying
 * does not reset the driver or make an unknown device state safe, it only gives
 * the driver another chance to answer.
 * 
 * @param adapter Failed or quarantined adapter.
 * @return OS_EOK when retry is scheduled, or OS_EINVALPARAMS if null or in a
 *         state that cannot be retried.
 */
__EXTERN oserr_t
NetAdapterRetry(
    _In_ NetworkAdapter_t* adapter);

/** 
 * @brief Schedule an asynchronous refresh of cached traffic counters.
 * Performs no IPC directly. Read the cached values with NetAdapterSnapshot.
 * Null is ignored.
 * 
 * @param adapter Adapter whose counters should be refreshed.
 */
__EXTERN void
NetAdapterRefreshCounters(
    _In_ NetworkAdapter_t* adapter);

/** 
 * @brief Copy cached state, batch progress, link status, counters, and buffer stats.
 * Nonblocking, triggers no IPC. If either pointer is null, nothing is written.
 * Otherwise, out is zeroed before filling.
 * 
 * @param adapter Adapter to inspect.
 * @param out Destination snapshot.
 */
__EXTERN void
NetAdapterSnapshot(
    _In_  const NetworkAdapter_t* adapter,
    _Out_ NetAdapterSnapshot_t*   out);

/**
 * @brief Copy the status summary passed to State callbacks.
 * Used by the registry to catch up a consumer that attaches after startup.
 *
 * @param adapter Adapter to inspect.
 * @param statusOut Destination status struct. Zeroed before filling.
 */
__EXTERN void
NetAdapterGetStatus(
    _In_  const NetworkAdapter_t* adapter,
    _Out_ NetAdapterStatus_t*     statusOut);

/**
 * @brief Move the adapter to a new state and invoke the State callback if it changed.
 * Does nothing if the adapter is already in that state.
 *
 * @param adapter Adapter to update.
 * @param state Target state.
 */
__EXTERN void
NetAdapterSetState(
    _In_ NetworkAdapter_t*    adapter,
    _In_ enum NetAdapterState state);

/** 
 * @brief Handle an untrusted or unparseable control reply from the transport layer
 * (for example, a failed decoder call or mismatched serial).
 *
 * If the pending request is OPEN or CLOSE, the adapter moves to QUARANTINED
 * because the driver may still hold a session or access shared memory. Any other
 * pending request fails normally. Null is ignored. Do not call this for a normal
 * error code returned inside a valid driver reply.
 * 
 * @param adapter Adapter reporting the protocol error.
 */
__EXTERN void
NetAdapterSetProtocolError(
    _In_ NetworkAdapter_t* adapter);

/** 
 * @brief Apply a link state update from the driver.
 *
 * Ignored unless it comes from the adapter's driver and current session. Older
 * sequence numbers are quietly ignored so cached state never rolls back. Reusing
 * the current sequence number with different link values fails as a protocol
 * error. Valid newer reports update the cache and fire the Link callback.
 * 
 * @param adapter Adapter receiving the update.
 * @param driver Source driver UUID.
 * @param session Source session ID and generation.
 * @param link Link state snapshot.
 * @return OS_EOK when applied or an older report is ignored, OS_ENOENT if the
 *         session does not match or is closing, OS_EINVALPARAMS if link is null,
 *         or OS_EPROTOCOL for invalid or conflicting link data.
 */
__EXTERN oserr_t
NetAdapterLinkChanged(
    _In_ NetworkAdapter_t*                    adapter,
    _In_ uuid_t                               driver,
    _In_ const struct ctt_netadapter_session* session,
    _In_ const struct ctt_netadapter_link*    link);

#endif

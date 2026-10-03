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
 * Core API for one netd adapter session. The core does not depend on how messages
 * are carried, so it can be driven by the Gracht transport as well as directly by
 * host tests.
 *
 * The work is split across several files: adapter.c manages the session lifecycle
 * and request state, buffers.c keeps track of which pool slots are leased out,
 * queue.c handles submitting packets and processing their completions, rx.c decides
 * who manages received packets, requests.c decides which protocol request to send
 * next, and client.c sends and receives the Gracht messages. adapters.c serializes
 * access so netd service code can safely call in from several threads.
 *
 * Each adapter is driven by a single worker that handles one call at a time. The
 * request pump hands out at most one request to send per call. The answer to a
 * control request comes back as a NetAdapterReply_t, while messages the driver sends
 * on its own (batch admissions, acknowledgement progress and packet completions)
 * arrive as a NetAdapterEvent_t.
 * When a request times out, the exact same operation or batch is sent again with
 * the same identity. A timeout never causes the packets to be submitted again as a
 * new batch, because the driver may already have processed the first copy.
 * Callbacks are invoked synchronously on the worker and must not call back into
 * this API. The driver and session checks route messages to the right session and
 * reject old messages; they are not a form of OS authentication.
 */

#ifndef __NETMANAGER_ADAPTER_H__
#define __NETMANAGER_ADAPTER_H__

#include "buffers.h"

#define NET_ADAPTER_BATCH_MAX CTT_NETADAPTER_LIMIT_BATCH_RECORDS
#define NET_ADAPTER_WINDOW_MAX 8

// Forward declarations
typedef struct NetworkAdapter NetworkAdapter_t;

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

/** Per-port resource and timing limits used when negotiating and running a session.
 * An MTU of zero selects min(current_mtu, NET_ADAPTER_MTU_DEFAULT), raised to the
 * driver's minimum MTU if it is lower than that. The configured MTU does not include
 * the 14-byte Ethernet header. Slot counts are the most we ask for; if the driver
 * supports fewer, the driver's limits are used instead.
 */
typedef struct NetAdapterConfig {
    uint32_t TxSlots;            // Requested TX pool slots; must be nonzero.
    uint32_t RxSlots;            // Requested RX pool slots; must be nonzero and meet the driver's minimum.
    uint32_t Mtu;                // Payload MTU, or zero to select the negotiated default.
    uint64_t MemoryBudget;       // Total budget for buffers and per-session bookkeeping; must be nonzero.
    uint32_t RetryMilliseconds;  // How long to wait for an answer before resending, in milliseconds; must be nonzero.
    uint32_t RetryLimit;         // Maximum times a request is sent, including the first; must be nonzero.
    uint32_t PollMilliseconds;   // Interval between asynchronous status polls; must be nonzero.
    uint32_t RxRetainedSlots;    // Most RX pool slots consumers may hold at once; zero means they may hold none.
    uint32_t RxCopySlots;        // RX copy slots allocated up front, used when consumers hold too many pool slots; zero disables copies.
    uint32_t BatchWindow;        // Most batches sent but not yet finished at once: 1..WINDOW_MAX, and at most the driver's limit.
} NetAdapterConfig_t;

/** 
 * @brief Describes where the memory behind a packet comes from. This is only used
 * inside netd and is never sent to the driver.
 *   * NONE marks a cleared or invalid packet.
 *   * POOL means the packet uses a slot leased from the shared memory pool that is
 *     registered with the driver.
 *   * COPY means the packet was copied into one of the RX copy slots that the
 *     adapter allocated up front.
 * 
 * The tag selects which of the two storage variants in NetAdapterPacketIdentity_t is
 * valid. Code outside the adapter must treat all of it as opaque, including Owner.
 */
enum NetAdapterPacketBacking {
    NET_ADAPTER_PACKET_NONE,
    NET_ADAPTER_PACKET_POOL,
    NET_ADAPTER_PACKET_COPY
};

typedef struct NetAdapterPacketIdentity {
    NetworkAdapter_t* Owner;
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
 * @brief A read-only received packet offered through ReceivePacket. Only one party
 * manages it at a time and it is not reference counted. If the consumer accepts it,
 * it should copy this value once into its own queue and later release it exactly
 * once with NetAdapterRxRelease. The payload stays valid even if the adapter is
 * stopped or closed in the meantime. The Private fields are opaque; they tie the
 * packet to either a pool lease or an RX copy slot of the adapter that produced it.
 */
typedef struct NetAdapterRxPacket {
    const void*                Data;
    uint32_t                   Length;
    NetAdapterPacketIdentity_t Private;
} NetAdapterRxPacket_t;

/** Release a packet accepted by ReceivePacket, giving its pool slot or copy slot
 * back to the adapter. The packet stays valid through stop and close until it is
 * released. Call this from the adapter's serialized worker context, but never from
 * inside a receive callback; to refuse a packet, return false from ReceivePacket
 * instead. Code running outside the worker can use the synchronized wrapper
 * provided by the netd service.
 * @param adapter The adapter that produced the packet.
 * @param packet The accepted token; cleared on success.
 * @return OS_EOK on release, OS_EINVALPARAMS for null inputs, or OS_ENOENT if the
 * token is out of date, was already released, or belongs to another adapter.
 */
__EXTERN oserr_t
NetAdapterRxRelease(
    _In_ NetworkAdapter_t*     adapter,
    _In_ NetAdapterRxPacket_t* packet);

/** Optional notifications, invoked synchronously on the adapter's serialized worker.
 * Callbacks must not block or call back into this API. The bytes passed to Receive
 * are only valid during the call. Transmitted is called exactly once for every send
 * the adapter accepted, when it is completed, rejected or cancelled; it is not
 * called when the submit call itself fails. A successful transmit means the device
 * has finished reading the frame from memory (DMA), not that the peer received it.
 * Link is called with link state that has been validated and that is newer than the
 * last one reported (every link report carries an increasing sequence number).
 */
typedef struct NetAdapterCallbacks {
    /**
     * @brief Receive an incoming frame when packet delivery to the consumer is unavailable
     * or declined. The frame bytes are borrowed only until this callback returns.
     * @param context User-defined context passed to all callback functions.
     * @param frame Pointer to the received frame data.
     * @param length Length of the received frame data.
     */
    void (*Receive)(void* context, const void* frame, uint32_t length);

    /**
    * @brief Report the final result of a frame accepted for transmission.
     * @param context User-defined context passed to all callback functions.
     * @param cookie The user cookie associated with the transmission.
    * @param status Completion, rejection, or cancellation status; success means
    * the device finished DMA, not that the frame was delivered remotely.
     */
    void (*Transmitted)(void* context, uint64_t cookie, oserr_t status);

    /**
    * @brief Report a validated link snapshot with a newer sequence number.
     * @param context User-defined context passed to all callback functions.
    * @param link The current carrier, duplex, and speed snapshot; borrowed for
    * the duration of the callback.
     */
    void (*Link)(void* context, const struct ctt_netadapter_link* link);
    
    /** 
     * @brief The preferred way to deliver received packets. Return true to accept the
     * packet; the consumer then manages it and must release it later. Return false
     * to decline it. The callback must not block or call back into this API. If an
     * accepted packet is handed to another thread, the consumer must provide the
     * synchronization for that itself. Consumers may only hold a limited number of
     * pool slots at once; once that limit is reached, the packet offered is a copy
     * stored in one of a fixed number of copy slots that were allocated up front.
     * If this hook is not set, or the packet is declined or cannot be offered,
     * Receive is called instead with a view that is only valid during that call.
     * If there is no Receive hook either, the frame is dropped and its pool slot
     * is reused.
     * @param context User-defined context passed to all callback functions.
     * @param packet The RX packet being offered for delivery.
     * @return True if the packet is accepted for delivery, false otherwise.
     */
    bool (*ReceivePacket)(void* context, const NetAdapterRxPacket_t* packet);

    // User-defined context passed to all callback functions.
    void* Context;
} NetAdapterCallbacks_t;

/** A fixed-size request produced by the request pump for the transport to send. Only
 * the fields used by Operation have meaning. Serial is used to match a control
 * request with its response and stays the same when the request is resent. The
 * returned storage is only valid until the next call into the adapter core.
 * When a batch is resent, its batch ID, run and packet descriptors stay exactly the
 * same so the driver can recognize it as a repeat. Only the acknowledgement (Ack)
 * that is sent along with the batch may be newer, because it reports how far we
 * have processed the driver's results.
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
    // Registration ID, batch ID or drain request ID. For starting or stopping a run,
    // the ID of the last batch the driver has accepted.
    uint64_t                      Value;
    uint64_t                      Run;
    uint64_t                      After;
    struct ctt_netadapter_ack    Ack;
    struct ctt_netadapter_pool   Pool;
    struct ctt_netadapter_packet Packets[NET_ADAPTER_BATCH_MAX];
} NetAdapterRequest_t;

/** Decoded response payload passed with its driver and request serial to
 * HandleAdapterRequest. Only the fields defined for that operation are meaningful.
 */
typedef struct NetAdapterReply {
    oserr_t                        Status;
    struct ctt_netadapter_info     Info;
    struct ctt_netadapter_session  Session;
    struct ctt_netadapter_link     Link;
    struct ctt_netadapter_counters Counters;
    // Enabled features for OPEN. For STOP_RUN, the sequence number of the last
    // completion the driver will report for the run. Or a drain's highest sequence.
    uint64_t                       Value;
    uint32_t                       PoolId;
} NetAdapterReply_t;

/** Nonblocking copy of the adapter's cached lifecycle, link, counters, and resource
 * state. Buffer statistics remain zero until buffer pools have been created.
 */
typedef struct NetAdapterSnapshot {
    enum NetAdapterState           State;
    oserr_t                        LastError;
    struct ctt_netadapter_info     Info;
    struct ctt_netadapter_link     Link;
    struct ctt_netadapter_counters Counters;
    NetBufferStats_t               Buffers;
    uint64_t                       Run;
    uint64_t                       AdmittedBatch;
    uint64_t                       RetiredBatch;
    uint32_t                       PendingBatches;
    uint32_t                       RxPoolRetained;
    uint32_t                       RxCopyRetained;
    uint64_t                       RxFallbackCopies;
    uint64_t                       RxDropped;
} NetAdapterSnapshot_t;

/** An event from the driver, copied by the transport callback once the generated
 * decoder has decoded it successfully. For an admission event (the driver telling us
 * which packets of a batch it accepted) Id is the batch ID; for completion and drain
 * events it is the request ID. The arrays from the generated decoder are only valid
 * during the decoder callback, so check Count against NET_ADAPTER_BATCH_MAX and copy
 * them into this value before the callback returns.
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
 * @brief A writable TX packet backed by a pool lease. Treat the Private fields as opaque.
 * Only one party manages this value at a time and it is not reference counted, so do
 * not copy it or use it from several threads at once. Data points at the start of
 * the Ethernet header. Capacity does not include any extra space the device keeps
 * before or after the frame for its own use, so build all headers within Capacity.
 * The view stays valid, and this value stays set, until a successful Submit or
 * Cancel clears it.
 */
typedef struct NetAdapterTxPacket {
    void*                      Data;
    uint32_t                   Capacity;
    NetAdapterPacketIdentity_t Private;
} NetAdapterTxPacket_t;

/** Initialize conservative per-port defaults: 32 slots per direction, a 1 MiB memory
 * budget, up to eight RX pool slots held by consumers, eight RX copy slots, four
 * batches in flight and up to three attempts per request. RX limits are reduced
 * later if the driver supports less.
 * @param config Destination configuration; null is ignored.
 */
__EXTERN void
NetAdapterConfigInitializeDefault(
    _In_ NetAdapterConfig_t* config);

/** Allocate an idle adapter and copy its configuration and optional callbacks. This
 * creates only local state; the request pump later starts by asking the driver for
 * its capabilities.
 * @param device Device identity to discover.
 * @param driver Driver endpoint identity used to route and validate replies.
 * @param port Per-device port index to open.
 * @param config Required configuration, copied into the adapter.
 * @param callbacks Optional callback table, copied when non-null.
 * @param out Receives the new adapter; set to null on failure.
 * @return OS_EOK, OS_EINVALPARAMS for invalid inputs/configuration, or OS_EOOM.
 */
__EXTERN oserr_t
NetAdapterCreate(
    _In_  uuid_t                       device,
    _In_  uuid_t                       driver,
    _In_  uint32_t                     port,
    _In_  const NetAdapterConfig_t*    config,
    _In_  const NetAdapterCallbacks_t* callbacks,
    _Out_ NetworkAdapter_t**           out);

/** Replace the callback hooks. Must be called from the adapter's serialized worker
 * context. A null callback table clears
 * all hooks. This affects future notifications only; packets already accepted by
 * ReceivePacket remain managed by the caller and must still be released.
 * @param adapter Adapter whose callbacks are changed; null is ignored.
 * @param callbacks New callback table, or null to clear all callbacks.
 */
__EXTERN void
NetAdapterSetCallbacks(
    _In_ NetworkAdapter_t*            adapter, 
    _In_ const NetAdapterCallbacks_t* callbacks);

/** Destroy an adapter that has finished all its work and release its local
 * resources. The adapter must be CLOSED or FAILED, consumers must have released
 * every received packet they kept, and no pool slots may still be in use. This
 * cannot be forced: if the outcome of an OPEN or CLOSE request is unknown, the
 * driver may still have a session and may still be using the shared memory for DMA,
 * so that memory must not be freed.
 * @param adapter Pointer to the adapter pointer; null pointer is invalid and a
 * null adapter value is already destroyed.
 * @return OS_EOK and clears *adapter on success, OS_EINVALPARAMS for a null
 * pointer, OS_EBUSY while the adapter is still active or any packet resource is
 * still in use, or a buffer-destruction error.
 */
__EXTERN oserr_t
NetAdapterDestroy(
    _In_ NetworkAdapter_t** adapter);

/** Apply one decoded control response to the matching outstanding request. The
 * response is only used if it comes from the adapter's driver, a request is
 * actually pending, and the serial matches; this keeps old or misrouted responses
 * from changing the lifecycle state. If the result of an OPEN or CLOSE cannot be
 * trusted, the adapter is quarantined and the request is kept, so a later retry can
 * resolve it with the same identity instead of forgetting a session the driver may
 * have created.
 * @param adapter Adapter with the outstanding request.
 * @param driver Source driver identity.
 * @param serial Serial of the request this response answers.
 * @param reply Decoded response payload.
 * @param now Current time in milliseconds, from a clock that never goes backwards;
 * used to schedule follow-up work.
 * @return OS_EOK when applied, OS_ENOENT when it matches no pending request,
 * OS_EINVALPARAMS for null inputs, the error reported by the driver, OS_EPROTOCOL
 * for a malformed response, or an error from local setup.
 */
__EXTERN oserr_t
HandleAdapterRequest(
    _In_ NetworkAdapter_t*        adapter,
    _In_ uuid_t                   driver,
    _In_ uint64_t                 serial,
    _In_ const NetAdapterReply_t* reply,
    _In_ uint64_t                 now);

/** Acquire a TX pool slot that the caller can build a frame in. This does not queue a
 * transmission; the writable view is managed only by the caller until a successful
 * Submit or Cancel. The adapter must be running and the link must be up.
 * Packets that are still being built keep their pool storage alive through stop and
 * close, so cancel any packet you no longer need.
 * @param adapter Running adapter.
 * @param packet Unused output token; cleared on failure.
 * @return OS_EOK, OS_EINVALPARAMS for null inputs, OS_ENOTCONNECTED when TX is
 * unavailable, or the underlying pool error.
 */
__EXTERN oserr_t
NetAdapterTxAcquire(
    _In_ NetworkAdapter_t*      adapter,
    _Out_ NetAdapterTxPacket_t* packet);

/** Queue a finished Ethernet frame from an acquired TX lease, without copying it.
 * Length includes the Ethernet header and must be between 14 and MTU+14. On success
 * the lease is handed to the adapter, the token is cleared, and the cookie is later
 * reported through Transmitted. On failure the token and lease stay with the caller.
 * A lease acquired before the adapter was stopped and started again cannot be
 * submitted in the new run; cancel it instead. This call must be serialized with
 * other core calls, but the frame itself may be built outside the worker while the
 * lease is managed only by the caller.
 * @param adapter Adapter the lease was acquired from.
 * @param packet Acquired TX token; consumed only on success.
 * @param length Complete Ethernet frame length in bytes.
 * @param cookie Caller value returned by the eventual Transmitted callback.
 * @return OS_EOK, OS_EINVALPARAMS for invalid length/input, OS_ENOENT for an invalid
 * token, OS_ENOTCONNECTED when the run has ended or the link is down, OS_EOVERFLOW
 * when the counter used to order queued frames has run out, or a pool error.
 */
__EXTERN oserr_t
NetAdapterTxSubmit(
    _In_ NetworkAdapter_t*     adapter,
    _In_ NetAdapterTxPacket_t* packet,
    _In_ uint32_t              length,
    _In_ uint64_t              cookie);

/** Release an acquired but unsubmitted TX lease, including after stop, close, or
 * link loss. This abandons the frame and does not call Transmitted.
 * @param adapter Adapter the lease was acquired from.
 * @param packet Acquired token; cleared only after successful release.
 * @return OS_EOK, OS_EINVALPARAMS for null inputs, OS_ENOENT for a token that is out
 * of date, belongs to another adapter or was already submitted, or a pool-release
 * error.
 */
__EXTERN oserr_t
NetAdapterTxCancel(
    _In_ NetworkAdapter_t*     adapter,
    _In_ NetAdapterTxPacket_t* packet);

/** Copy a complete Ethernet frame into a TX pool slot and queue it. This is a
 * convenience path equivalent to Acquire/copy/Submit; local rejection does not
 * produce a Transmitted callback. Requires a running adapter and carrier-up link.
 * @param adapter Running adapter.
 * @param frame Ethernet frame to copy; must remain readable for this call.
 * @param length Frame length in bytes, including the 14-byte Ethernet header.
 * @param cookie Caller value returned if the frame is accepted.
 * @return OS_EOK when queued, OS_EINVALPARAMS for invalid frame/input, or an
 * acquisition/submission error such as OS_ENOTCONNECTED or OS_EBUSY.
 */
__EXTERN oserr_t
NetAdapterSend(
    _In_ NetworkAdapter_t* adapter,
    _In_ const void*       frame,
    _In_ uint32_t          length,
    _In_ uint64_t          cookie);

/** Request an orderly stop of the current run. The request is recorded immediately,
 * so new TX acquires and submits are rejected from now on. The request pump then
 * carries out the stop asynchronously: it stops sending new batches, waits until the
 * driver has accepted or rejected every batch already sent, and sends STOP_RUN. The
 * driver answers with the sequence number of the last completion it will report for
 * the run; the pump then collects and acknowledges every completion up to that
 * number. The adapter must reach STOPPED before Start can begin another run. Null
 * is ignored.
 */
__EXTERN void
NetAdapterStop(
    _In_ NetworkAdapter_t* adapter);

/** Begin a new run from STOPPED. The request pump asynchronously prepares the run,
 * posts the initial receive buffers to the driver and starts it; this call does not
 * wait for the device to become active.
 * @param adapter Adapter to start.
 * @return OS_EOK when the new run is scheduled, OS_EINVALPARAMS for null,
 * OS_EBUSY unless STOPPED, or OS_EOVERFLOW if the run identifier is exhausted.
 */
__EXTERN oserr_t
NetAdapterStart(
    _In_ NetworkAdapter_t* adapter);

/** Request an orderly close; this also works while the adapter is still being set
 * up. The request is recorded immediately and new TX is rejected from now on. If an
 * OPEN request is still unanswered, the request pump first resolves it by resending
 * it with its original identity, because the driver may already have created a
 * session. Storage is only freed after the driver has confirmed the close, since
 * only then is it certain the driver no longer uses the shared memory. Null is
 * ignored.
 */
__EXTERN void
NetAdapterClose(
    _In_ NetworkAdapter_t* adapter);

/** Try again to recover a failed adapter. If the adapter is QUARANTINED because the
 * outcome of an OPEN or CLOSE request is unknown, that same request is sent again
 * with its original identity, so the driver can recognize it as a repeat. If the
 * adapter FAILED before a session was opened and holds no session or buffers, it
 * starts over by asking the driver for its capabilities. Retrying does not reset the
 * driver, and it does not make an unknown outcome safe; it only gives the driver
 * another chance to answer.
 * @param adapter Adapter to recover.
 * @return OS_EOK when the retry is scheduled, OS_EINVALPARAMS for null or for any
 * state other than a recoverable FAILED or QUARANTINED.
 */
__EXTERN oserr_t
NetAdapterRetry(
    _In_ NetworkAdapter_t* adapter);

/** Mark the cached counters as out of date and request an asynchronous refresh. It does
 * not perform IPC in this call; use Snapshot to read the current cache. Null is ignored.
 */
__EXTERN void
NetAdapterRefreshCounters(
    _In_ NetworkAdapter_t* adapter);

/** Copy the cached lifecycle state, batch progress (how many batches the driver has
 * accepted and finished), link state, counters and pool usage.
 * This is a nonblocking read and schedules no IPC. If either pointer is null, no
 * output is written; otherwise the output is cleared before cached fields are copied.
 * @param adapter Adapter to inspect.
 * @param out Destination snapshot.
 */
__EXTERN void
NetAdapterSnapshot(
    _In_  const NetworkAdapter_t* adapter,
    _Out_ NetAdapterSnapshot_t*   out);

/** Report that an incoming control response could not be trusted (for example, the
 * generated decoder failed, or the response did not match the pending request).
 * If the pending request is an OPEN or CLOSE, the adapter is quarantined: we do not
 * know whether the driver created or closed the session, so its memory may still be
 * in use. Other requests go through the normal failure handling. Null is ignored.
 * This is not used for an error reply sent by the driver.
 */
__EXTERN void
NetAdapterSetProtocolError(
    _In_ NetworkAdapter_t* adapter);

/** Apply a link state report, but only if it comes from the adapter's driver and
 * current session. Reports with an older sequence number are ignored so the cached
 * state never goes back to an older value; a report that reuses the current sequence
 * number with different data is a protocol error. Valid newer state is cached and
 * passed to the Link callback.
 * @param adapter Adapter receiving the update.
 * @param driver Source driver identity.
 * @param session Source session identity and generation.
 * @param link Sequenced link snapshot.
 * @return OS_EOK when applied or an older report is ignored, OS_ENOENT when the
 * session does not match or is closing, OS_EINVALPARAMS for a null link, or
 * OS_EPROTOCOL for invalid or contradictory link data.
 */
__EXTERN oserr_t
NetAdapterLinkChanged(
    _In_ NetworkAdapter_t*                    adapter,
    _In_ uuid_t                               driver,
    _In_ const struct ctt_netadapter_session* session,
    _In_ const struct ctt_netadapter_link*    link);

#endif

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
 * Core API for one transport-independent netd adapter session.
 * The session executor owns lifecycle and request state; buffers.c tracks pool
 * leases, queue.c handles packet submission and completion, rx.c handles receive
 * ownership, requests.c schedules protocol operations, and client.c provides
 * Gracht transport. adapters.c serializes access for netd service callers.
 * 
 * netd adapter sessions. One serialized executor owns each instance.
 *
 * The core is transport-independent: NextRequest offers one publication; control
 * results use Reply and asynchronous batch/credit/completion events use Event.
 * Deadlines retry the SAME operation/batch, never a new descriptor submission.
 * Callbacks run synchronously on that executor and must not reenter this API.
 * Endpoint/session checks protect routing and stale messages, not OS authentication.
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
 * Zero MTU selects min(current_mtu, NET_ADAPTER_MTU_DEFAULT), clamped to the
 * advertised range. Configured MTU and frame lengths exclude the 14-byte Ethernet
 * header. Slot counts are upper bounds and are capped to negotiated limits.
 */
typedef struct NetAdapterConfig {
    uint32_t TxSlots;            // Requested TX pool slots; must be nonzero.
    uint32_t RxSlots;            // Requested RX pool slots; must be nonzero and meet the driver's minimum.
    uint32_t Mtu;                // Payload MTU, or zero to select the negotiated default.
    uint64_t MemoryBudget;       // Total budget for buffers and per-session bookkeeping; must be nonzero.
    uint32_t RetryMilliseconds;  // Delay between retries, in monotonic milliseconds; must be nonzero.
    uint32_t RetryLimit;         // Maximum publication attempts, including the first; must be nonzero.
    uint32_t PollMilliseconds;   // Interval between asynchronous status polls; must be nonzero.
    uint32_t RxRetainedSlots;    // Maximum pool-backed RX packets retained by callers; zero disables retention.
    uint32_t RxCopySlots;        // Preallocated RX-copy fallback slots; zero disables copied retention.
    uint32_t BatchWindow;        // In-flight batch limit: 1..WINDOW_MAX, capped to the driver's replay limit.
} NetAdapterConfig_t;

/** 
 * @brief Local packet backing, never sent over IPC. 
 *   * NONE is the cleared/invalid token.
 *   * POOL is a shared memory pool lease.
 *   * COPY is a preallocated RX fallback slot.
 * 
 * The tag selects exactly one identity: a SHM pool lease or an RX fallback slot.
 * Consumers treat the backing as opaque, including the owner.
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
 * @brief Single-owner, read-only RX packet offered by ReceivePacket. On acceptance,
 * copy this value once into the consumer's queue and release it exactly once.
 * Payload stays valid across stop/close. Private fields are opaque; they bind
 * either a pool lease or a preallocated copy to its owning local adapter.
 */
typedef struct NetAdapterRxPacket {
    const void*                Data;
    uint32_t                   Length;
    NetAdapterPacketIdentity_t Private;
} NetAdapterRxPacket_t;

/** Release a packet accepted by ReceivePacket, returning its pool lease or copy
 * slot to the adapter. The packet remains valid across stop/close until released.
 * Call under core serialization and never from a receive callback; return false
 * from ReceivePacket to decline instead. The synchronized service wrapper may be
 * used outside callbacks.
 * @param adapter The packet's owning adapter.
 * @param packet The accepted token; cleared on success.
 * @return OS_EOK on release, OS_EINVALPARAMS for null inputs, or OS_ENOENT for a
 * stale, duplicate, or foreign token.
 */
__EXTERN oserr_t
NetAdapterRxRelease(
    _In_ NetworkAdapter_t*     adapter,
    _In_ NetAdapterRxPacket_t* packet);

/** Optional notifications invoked synchronously on the serialized adapter executor.
 * Callbacks must not block or reenter this API. Receive bytes are borrowed only
 * for the call. Transmitted is issued once for an accepted send when completed,
 * rejected, or cancelled; it is not issued for local submission failure. A
 * successful transmit means DMA completed, not that the peer received the frame.
 * Link supplies validated, sequenced carrier snapshots.
 */
typedef struct NetAdapterCallbacks {
    /**
     * @brief Receive an incoming frame when owned-packet delivery is unavailable
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
     * @brief Preferred RX delivery. True transfers one owned packet to the consumer;
     * false declines it. No reentry/blocking is allowed. Publish an accepted
     * value to another thread with caller-provided synchronization. When pool
     * retention is full, the offered value uses bounded preallocated copy storage.
     * If delivery is unavailable/declined, Receive gets a borrowed view instead;
     * with no Receive hook the frame is dropped and its pool slot recycled.
     * @param context User-defined context passed to all callback functions.
     * @param packet The RX packet being offered for delivery.
     * @return True if the packet is accepted for delivery, false otherwise.
     */
    bool (*ReceivePacket)(void* context, const NetAdapterRxPacket_t* packet);

    // User-defined context passed to all callback functions.
    void* Context;
} NetAdapterCallbacks_t;

/** Fixed-sized request envelope returned by the request pump. Only fields used by
 * Operation are meaningful. Serial correlates a control response and is unchanged
 * on retry. Returned storage is borrowed until the next core call. Retried batch,
 * run, and descriptor identities remain fixed; piggyback ACK progress may advance.
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
    // registration ID, batch ID, drain request ID, or lifecycle fence
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
    // enabled features, stop barrier, or drain highest sequence
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

/** Event envelope copied by the transport callback after generated decoding
 * succeeds. Id is a batch ID for admission and a request ID for completion/drain.
 * Copy borrowed generated arrays into this value before the decoder callback
 * returns, after checking Count against NET_ADAPTER_BATCH_MAX.
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
 * @brief Writable TX packet backed by a pool lease. Treat the Private fields as opaque.
 * This is a single-owner value, not a reference-counted packet: do not copy it or
 * use it concurrently. Data starts at the Ethernet header; Capacity excludes
 * device-private headroom/tailroom. Build all headers within this frame space.
 * Only successful Submit or Cancel invalidates the view and clears this value.
 */
typedef struct NetAdapterTxPacket {
    void*                      Data;
    uint32_t                   Capacity;
    NetAdapterPacketIdentity_t Private;
} NetAdapterTxPacket_t;

/** Initialize conservative per-port defaults: 32 slots per direction, 1 MiB total
 * budget, eight retained RX leases, eight fallback copies, four in-flight batches,
 * and three publication attempts. RX limits are later capped to negotiated capacity.
 * @param config Destination configuration; null is ignored.
 */
__EXTERN void
NetAdapterConfigInitializeDefault(
    _In_ NetAdapterConfig_t* config);

/** Allocate an idle adapter and copy its configuration and optional callbacks. This
 * creates only local state; the request pump begins capability discovery later.
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

/** Replace callback hooks under core serialization. A null callback table clears
 * all hooks. This affects future notifications only; packets already accepted by
 * ReceivePacket remain caller-owned and must still be released.
 * @param adapter Adapter whose callbacks are changed; null is ignored.
 * @param callbacks New callback table, or null to clear all callbacks.
 */
__EXTERN void
NetAdapterSetCallbacks(
    _In_ NetworkAdapter_t*            adapter, 
    _In_ const NetAdapterCallbacks_t* callbacks);

/** Destroy a quiescent adapter and release its local resources. An adapter must be
 * CLOSED or FAILED with no retained RX packets or outstanding buffer ownership;
 * uncertain OPEN/CLOSE and DMA-owned memory cannot be force-freed.
 * @param adapter Pointer to the adapter pointer; null pointer is invalid and a
 * null adapter value is already destroyed.
 * @return OS_EOK and clears *adapter on success, OS_EINVALPARAMS for a null
 * pointer, OS_EBUSY while the adapter or any packet resource is still owned, or
 * a buffer-destruction error.
 */
__EXTERN oserr_t
NetAdapterDestroy(
    _In_ NetworkAdapter_t** adapter);

/** Apply one decoded control response to the matching outstanding request. Driver,
 * pending-operation, and serial checks prevent stale or misrouted responses from
 * changing lifecycle state. OPEN/CLOSE uncertainty is preserved for safe recovery.
 * @param adapter Adapter with the outstanding request.
 * @param driver Source driver identity.
 * @param serial Correlation serial returned with the response.
 * @param reply Decoded response payload.
 * @param now Current monotonic time in milliseconds, used to schedule follow-up work.
 * @return OS_EOK when applied, OS_ENOENT when unmatched, OS_EINVALPARAMS for null
 * inputs, or the reported/protocol/setup error.
 */
__EXTERN oserr_t
HandleAdapterRequest(
    _In_ NetworkAdapter_t*        adapter,
    _In_ uuid_t                   driver,
    _In_ uint64_t                 serial,
    _In_ const NetAdapterReply_t* reply,
    _In_ uint64_t                 now);

/** Acquire a preallocated TX pool lease for caller-side frame construction. This
 * does not queue a transmission; the caller exclusively owns the writable view
 * until successful Submit or Cancel. The adapter must be running with carrier up.
 * Outstanding builders pin storage through stop/close, so cancel unused packets.
 * @param adapter Running adapter.
 * @param packet Unused output token; cleared on failure.
 * @return OS_EOK, OS_EINVALPARAMS for null inputs, OS_ENOTCONNECTED when TX is
 * unavailable, or the underlying pool error.
 */
__EXTERN oserr_t
NetAdapterTxAcquire(
    _In_ NetworkAdapter_t*      adapter,
    _Out_ NetAdapterTxPacket_t* packet);

/** Queue a completed Ethernet frame from an acquired TX lease without copying.
 * Length includes the Ethernet header and must be 14..MTU+14. Success transfers
 * the lease to the adapter, clears the token, and eventually reports the cookie;
 * failure leaves the token and lease with the caller. A lease acquired in an old
 * run cannot be submitted after restart. Serialize this call with other core calls;
 * frame construction may occur outside the executor while the lease is exclusively owned.
 * @param adapter Owning adapter.
 * @param packet Acquired TX token; consumed only on success.
 * @param length Complete Ethernet frame length in bytes.
 * @param cookie Caller value returned by the eventual Transmitted callback.
 * @return OS_EOK, OS_EINVALPARAMS for invalid length/input, OS_ENOENT for an invalid
 * token, OS_ENOTCONNECTED when the run/link is no longer usable, OS_EOVERFLOW for
 * exhausted queue ordering, or a pool error.
 */
__EXTERN oserr_t
NetAdapterTxSubmit(
    _In_ NetworkAdapter_t*     adapter,
    _In_ NetAdapterTxPacket_t* packet,
    _In_ uint32_t              length,
    _In_ uint64_t              cookie);

/** Release an acquired but unsubmitted TX lease, including after stop, close, or
 * link loss. This abandons the frame and does not issue Transmitted.
 * @param adapter Owning adapter.
 * @param packet Acquired token; cleared only after successful release.
 * @return OS_EOK, OS_EINVALPARAMS for null inputs, OS_ENOENT for a stale, foreign,
 * or already-submitted token, or a pool-release error.
 */
__EXTERN oserr_t
NetAdapterTxCancel(
    _In_ NetworkAdapter_t*     adapter,
    _In_ NetAdapterTxPacket_t* packet);

/** Copy a complete Ethernet frame into a bounded TX lease and queue it. This is a
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

/** Request an orderly stop of the current run. This records stop intent immediately
 * so new TX acquisition/submission is rejected; the request pump asynchronously
 * fences publication, resolves work, drains and ACKs the driver's stop barrier.
 * The adapter must reach STOPPED before Start can begin another run. Null is ignored.
 */
__EXTERN void
NetAdapterStop(
    _In_ NetworkAdapter_t* adapter);

/** Begin a new run from STOPPED. The request pump prepares the run, primes RX, and
 * starts it asynchronously; this call does not wait for the device to become active.
 * @param adapter Adapter to start.
 * @return OS_EOK when the new run is scheduled, OS_EINVALPARAMS for null,
 * OS_EBUSY unless STOPPED, or OS_EOVERFLOW if the run identifier is exhausted.
 */
__EXTERN oserr_t
NetAdapterStart(
    _In_ NetworkAdapter_t* adapter);

/** Request orderly closure, including during partial initialization. This records
 * close intent and prevents new TX immediately; the request pump must resolve any
 * outstanding OPEN using its original identity and establish safe remote close
 * before storage can be reclaimed. Null is ignored.
 */
__EXTERN void
NetAdapterClose(
    _In_ NetworkAdapter_t* adapter);

/** Resume recovery without changing the identity of an uncertain remote operation.
 * A pre-session FAILED adapter with no session or buffers instead restarts capability
 * discovery. Retrying does not reset the driver or make an uncertain outcome safe.
 * @param adapter Adapter to recover.
 * @return OS_EOK when retry is scheduled, OS_EINVALPARAMS for null or a state other
 * than recoverable FAILED/QUARANTINED.
 */
__EXTERN oserr_t
NetAdapterRetry(
    _In_ NetworkAdapter_t* adapter);

/** Mark cached counters stale and request an asynchronous counters refresh. It does
 * not perform IPC in this call; use Snapshot to read the current cache. Null is ignored.
 */
__EXTERN void
NetAdapterRefreshCounters(
    _In_ NetworkAdapter_t* adapter);

/** Copy cached lifecycle, completion-journal progress, link/counters, and pool usage.
 * This is a nonblocking read and schedules no IPC. If either pointer is null, no
 * output is written; otherwise the output is cleared before cached fields are copied.
 * @param adapter Adapter to inspect.
 * @param out Destination snapshot.
 */
__EXTERN void
NetAdapterSnapshot(
    _In_  const NetworkAdapter_t* adapter,
    _Out_ NetAdapterSnapshot_t*   out);

/** Report that an incoming control response could not be trusted (for example,
 * generated decoding or correlation validation failed). OPEN/CLOSE are quarantined
 * because the remote ownership result is uncertain; other operations use normal
 * failure handling. Null is ignored. This does not represent a remote error reply.
 */
__EXTERN void
NetAdapterSetProtocolError(
    _In_ NetworkAdapter_t* adapter);

/** Apply a carrier snapshot only when driver and session identity match. Older
 * sequence numbers are ignored to prevent rollback; conflicting data at the same
 * sequence is a protocol error. Valid newer state is cached and delivered to Link.
 * @param adapter Adapter receiving the update.
 * @param driver Source driver identity.
 * @param session Source session identity and generation.
 * @param link Sequenced link snapshot.
 * @return OS_EOK when applied or an older report is ignored, OS_ENOENT for a
 * nonmatching/closed session, OS_EINVALPARAMS for a null link, or OS_EPROTOCOL for
 * invalid or contradictory link data.
 */
__EXTERN oserr_t
NetAdapterLinkChanged(
    _In_ NetworkAdapter_t*                    adapter,
    _In_ uuid_t                               driver,
    _In_ const struct ctt_netadapter_session* session,
    _In_ const struct ctt_netadapter_link*    link);

#endif

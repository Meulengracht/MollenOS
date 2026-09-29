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

/** 
 * @brief Limits apply per port. 
 *   MTU zero selects min(current_mtu, NET_ADAPTER_MTU_DEFAULT), clamped to the
 *   advertised range. 
 *   Slot counts are upper bounds, capped to adapter limits.
 *   RetryLimit includes the first transmission. Time inputs are monotonic millis.
 */
typedef struct NetAdapterConfig {
    uint32_t TxSlots;
    uint32_t RxSlots;
    uint32_t Mtu;
    uint64_t MemoryBudget;
    uint32_t RetryMilliseconds;
    uint32_t RetryLimit;
    uint32_t PollMilliseconds;
    uint32_t RxRetainedSlots; // Zero disables pool-backed retention; clamped to preserve min_rx_slots.
    uint32_t RxCopySlots;     // Preallocated fallback packets; zero disables copied retention.
    uint32_t BatchWindow; // 1..WINDOW_MAX, also capped to the driver replay limit
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

/** Release an accepted RX packet under core serialization. Success clears it;
 * stale, duplicate and foreign-adapter tokens are rejected. Never call from a
 * receive callback: return false to decline instead. Service users call the
 * synchronized NetworkAdaptersRxRelease wrapper from outside callbacks.
 */
__EXTERN oserr_t
NetAdapterRxRelease(
    _In_ NetworkAdapter_t*     adapter,
    _In_ NetAdapterRxPacket_t* packet);

/** 
 * @brief RX bytes are borrowed for the duration of Receive only; copy if retaining.
 * Transmitted reports the user cookie once, on completion/rejection/cancellation;
 * successful completion means DMA finished, not remote delivery. No callback is
 * made for a Send rejected locally. Link reports sequenced carrier snapshots.
 */
typedef struct NetAdapterCallbacks {
    /**
     * @brief Reception callback for incoming frames.
     * @param context User-defined context passed to all callback functions.
     * @param frame Pointer to the received frame data.
     * @param length Length of the received frame data.
     * @return None.
     */
    void (*Receive)(void* context, const void* frame, uint32_t length);

    /**
     * @brief Transmission completion callback.
     * @param context User-defined context passed to all callback functions.
     * @param cookie The user cookie associated with the transmission.
     * @param status The status of the transmission.
     * @return None.
     */
    void (*Transmitted)(void* context, uint64_t cookie, oserr_t status);

    /**
     * @brief Link status update callback.
     * @param context User-defined context passed to all callback functions.
     * @param link The current link status.
     * @return None.
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

/**
 * @brief Fixed-sized transport envelope. Only fields used by Operation are meaningful.
 * Operation is a SERVICE_CTT_NETADAPTER_*_ID from the generated protocol.
 * Serial is a local correlation ID, unchanged on retries. Count <= BATCH_MAX.
 * Returned storage is borrowed until the next core call; copy if retaining it.
 * Batch/run/descriptor identity stays fixed on retry; piggyback ACKs may advance.
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

/** 
 * @brief Owned event envelope populated only after generated Gracht decoding succeeds.
 * Id means batch_id for admission and request_id for completion/drain events.
 * Event callbacks may borrow generated arrays only during decoding: copy them
 * here before returning. Count must fit BATCH_MAX before copying any element.
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

/** 
 * @brief Initialize conservative per-port defaults: 32 slots per direction, 1 MiB total
 * budget, eight retained RX leases, eight fallback copies, four in-flight batches
 * and three publication attempts. RX limits are clamped to negotiated capacity. */
__EXTERN void
NetAdapterConfigInitializeDefault(
    _In_ NetAdapterConfig_t* config);

/** 
 * @brief Allocate an idle core and copy config/callbacks. No IPC or remote allocation
 * occurs here; NextRequest starts capability discovery. On failure *out is NULL. */
__EXTERN oserr_t
NetAdapterCreate(
    _In_  uuid_t                       device,
    _In_  uuid_t                       driver,
    _In_  uint32_t                     port,
    _In_  const NetAdapterConfig_t*    config,
    _In_  const NetAdapterCallbacks_t* callbacks,
    _Out_ NetworkAdapter_t**           out);

/** 
 * @brief Replace delivery hooks under core serialization. NULL clears them. Existing
 * accepted RX packets keep their ownership and must still be released normally.
 */
__EXTERN void
NetAdapterSetCallbacks(
    _In_ NetworkAdapter_t*            adapter, 
    _In_ const NetAdapterCallbacks_t* callbacks);

/** 
 * @brief Only CLOSED or FAILED before open can be destroyed. Unknown open/close and
 * DMA ownership and caller-owned TX/RX packets pin the instance (OS_EBUSY);
 * there is no force-free operation.
 */
__EXTERN oserr_t
NetAdapterDestroy(
    _In_ NetworkAdapter_t** adapter);

/** 
 * @brief Applies a correlated control response only. Wrong
 * driver/serial is rejected without affecting the current transaction. Duplicate
 * replies from an earlier transaction have no effect. Payload is copied as needed.
 */
__EXTERN oserr_t
HandleAdapterRequest(
    _In_ NetworkAdapter_t*        adapter,
    _In_ uuid_t                   driver,
    _In_ uint64_t                 serial,
    _In_ const NetAdapterReply_t* reply,
    _In_ uint64_t                 now);

/** 
 * @brief Acquire a writable packet without queuing it. No allocation occurs per packet.
 * The caller owns payload access until Submit/Cancel. Stop/close cannot reclaim
 * this storage; failure to cancel an unused packet therefore pins the session.
 * Pass an unused output value. On failure it is cleared. Running/carrier-up only.
 */
__EXTERN oserr_t
NetAdapterTxAcquire(
    _In_ NetworkAdapter_t*      adapter,
    _Out_ NetAdapterTxPacket_t* packet);

/** 
 * @brief Queue a completed Ethernet frame without copying. Success transfers ownership
 * and eventually produces Transmitted(cookie, status); never use Data thereafter.
 * Failure leaves ownership with the caller, which must retry or Cancel. Packets
 * acquired in a previous run cannot be submitted after stop/start. Core calls
 * require serialization; payload construction may occur outside the executor.
 */
__EXTERN oserr_t
NetAdapterTxSubmit(
    _In_ NetworkAdapter_t*     adapter,
    _In_ NetAdapterTxPacket_t* packet,
    _In_ uint32_t              length,
    _In_ uint64_t              cookie);

/**
 * @brief Return an unsubmitted packet, including after stop, close or link loss.
 * Produces no Transmitted callback. A stale/already submitted token is rejected.
 */
__EXTERN oserr_t
NetAdapterTxCancel(
    _In_ NetworkAdapter_t*     adapter,
    _In_ NetAdapterTxPacket_t* packet);

/** 
 * @brief Copy one Ethernet frame into a bounded TX lease. Running/carrier-up only.
 * OS_EBUSY applies backpressure. Cookie belongs to the caller, never to the driver.
 */
__EXTERN oserr_t
NetAdapterSend(
    _In_ NetworkAdapter_t* adapter,
    _In_ const void*       frame,
    _In_ uint32_t          length,
    _In_ uint64_t          cookie);

/**
 * @brief Stop forbids new sends immediately, resolves pending work, drains the stop
 * barrier and ACKs it. Start prepares a new run before reposting RX.
 * Close uses the protocol's safe-close barrier, including during partial setup.
 */
__EXTERN void
NetAdapterStop(
    _In_ NetworkAdapter_t* adapter);

/**
 * @brief Request a fresh run from STOPPED. Returns OS_EBUSY in any other state;
 * RX priming and the start barrier are performed asynchronously by NextRequest. */
__EXTERN oserr_t
NetAdapterStart(
    _In_ NetworkAdapter_t* adapter);

/** 
 * @brief Set close intent, including during partial initialization. An unresolved OPEN
 * must be resolved using its original identity before local storage can be freed. */
__EXTERN void
NetAdapterClose(
    _In_ NetworkAdapter_t* adapter);

/**
 * @brief Explicit operator retry of a quarantined operation, preserving its identity
 * and buffers, or capability rediscovery after a definitive pre-session failure.
 * Useful when a driver becomes responsive again; never implies reset.
 */
__EXTERN oserr_t
NetAdapterRetry(
    _In_ NetworkAdapter_t* adapter);

/**
 * @brief Schedule a read-only counters request. Snapshot remains a nonblocking cache read.
 */
__EXTERN void
NetAdapterRefreshCounters(
    _In_ NetworkAdapter_t* adapter);

/**
 * @brief Copy current lifecycle, journal progress and pool usage without scheduling work.
 */
__EXTERN void
NetAdapterSnapshot(
    _In_  const NetworkAdapter_t* adapter,
    _Out_ NetAdapterSnapshot_t*   out);

/**
 * @brief A malformed control response is uncertain, particularly for OPEN/CLOSE.
 * Never synthesize an authoritative remote rejection from a decode failure.
 */
__EXTERN void
NetAdapterSetProtocolError(
    _In_ NetworkAdapter_t* adapter);

/**
 * @brief Apply a sequenced carrier update to the matching session. Older updates are
 * ignored; contradictory state at the same sequence is a protocol error.
 */
__EXTERN oserr_t
NetAdapterLinkChanged(
    _In_ NetworkAdapter_t*                    adapter,
    _In_ uuid_t                               driver,
    _In_ const struct ctt_netadapter_session* session,
    _In_ const struct ctt_netadapter_link*    link);

#endif

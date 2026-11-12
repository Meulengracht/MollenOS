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
 */

#ifndef __ETHERNET_NETADAPTER_PRIVATE_H__
#define __ETHERNET_NETADAPTER_PRIVATE_H__

#include "netadapter.h"
#include <ds/list.h>
#include <os/shm.h>
#include <os/handle.h>
#include <ctt_netadapter_service_server.h>

DECL_STRUCT(EthernetPool);

// Fixed common-layer capacities, independent of MAC registration requirements.
// Registration keys per session, including pools that have been unregistered.
// Only one TX pool and one RX pool may be mapped at a time.
#define ETHERNET_POOLS 4
// Maximum number of packet slots in one pool.
#define ETHERNET_SLOTS 32
// Maximum number of packets accepted in one submit request.
#define ETHERNET_BATCH 16
// Number of recent submit results kept so a client can safely retry a request.
#define ETHERNET_WINDOW 4
// Number of completion records kept until the client acknowledges them.
// Admission counts outstanding packets against this limit before queueing them.
#define ETHERNET_JOURNAL 128
// Maximum shared-memory capacity accepted for one packet pool.
#define ETHERNET_POOL_BYTES (128 * 1024)
// Initial allocation for closed sessions remembered for retry handling.
#define ETHERNET_INITIAL_CLOSED_SESSIONS 16

/**
 * @brief Packet description, sequence and completion state for one pool slot.
 */
struct EthernetSlot {
    // Pool containing this slot. Valid while the pool is attached.
    EthernetPool_t*              Pool;
    // Most recently accepted packet description for this slot.
    struct ctt_netadapter_packet Packet;
    // Last used submission sequence, including rejected requests.
    uint64_t                     Sequence;
    // True while a queued packet still needs a completion.
    bool                         Owned;
};

/**
 * @brief Shared memory registered by a client as a group of packet slots.
 */
struct EthernetPool {
    // Attached client shared memory.
    OSHandle_t                 Memory;
    // CPU physical ranges used to reject overlapping pools, not to build descriptors.
    SHMSGTable_t               AliasGeometry;
    // Fixed direction and layout supplied at registration.
    struct ctt_netadapter_pool Description;
    // First selected byte relative to the imported token range.
    size_t                     BufferOffset;
    // Client key kept reserved until the session closes.
    uint64_t                   Registration;
    // Whether Memory is mapped and ready for packet copies.
    bool                       Mapped;
    // Per-slot sequence and in-flight packet state.
    EthernetSlot_t             Slots[ETHERNET_SLOTS];
};

/**
 * @brief Saved submit request and admission results used to answer retries.
 */
typedef struct EthernetBatch {
    // Client-provided identifier for this request.
    uint64_t                        Id;
    // Device run in which this request was received.
    uint64_t                        Run;
    // Direction shared by all packets in the request.
    enum ctt_netadapter_direction   Direction;
    // Number of packet entries saved below.
    uint32_t                        Count;
    // Original entries used to validate retries.
    struct ctt_netadapter_packet    Packets[ETHERNET_BATCH];
    // Saved per-packet results returned on retry.
    struct ctt_netadapter_admission Admissions[ETHERNET_BATCH];
} EthernetBatch_t;

/**
 * @brief Selected completion records and final status for one drain request.
 * Sending these records does not acknowledge or remove them from the journal.
 */
typedef struct EthernetDrain {
    oserr_t                         Status;
    uint64_t                        Highest;
    uint32_t                        Count;
    struct ctt_netadapter_completion Records[ETHERNET_BATCH];
} EthernetDrain_t;

enum EthernetState {
    // Session exists, but queues and packet pools are not ready for work.
    ETHERNET_OPENED,
    // Queues are ready. Receive buffers may be submitted before start.
    ETHERNET_PREPARED,
    // Device is active and may process transmit and receive packets.
    ETHERNET_RUNNING,
    // New work is blocked while the device is being reset.
    ETHERNET_STOPPING,
    // Device is inactive. Acknowledged work may be prepared for another run.
    ETHERNET_STOPPED
};

/**
 * @brief Client session, packet pools, and saved request and completion results.
 */
typedef struct EthernetSession {
    // Whether a client has an active session.
    bool                             Active;
    // Whether the client selected the supported MTU and receive filter.
    bool                             Configured;
    // Whether new packet requests are blocked after a device error.
    bool                             Faulted;
    // Connection that opened the session.
    gracht_conn_t                    Owner;
    // Kernel identity checked independently of the reply endpoint.
    uint64_t                         Principal;
    // Token clients must present to identify this session.
    struct ctt_netadapter_session    Identity;
    // Current session state.
    enum EthernetState               State;
    // Identifier for the current start/stop cycle.
    uint64_t                         Run;
    // Last accepted batch boundary recorded at start.
    uint64_t                         StartFence;
    // Batch boundary supplied when stopping this run.
    uint64_t                         StopFence;
    // Completion boundary that stop waits to report.
    uint64_t                         StopBarrier;
    // Identifier for the current completion-drain request.
    uint64_t                         DrainId;
    // Registration slots used, including unregistered keys.
    uint32_t                         PoolCount;
    // Accepted packets awaiting results. TX is index 0, RX is index 1.
    uint32_t                         Outstanding[2];
    // Accepted, completed, and acknowledged sequence positions.
    struct ctt_netadapter_progress   Progress;
    // Packet, byte, error, and queue-full totals.
    struct ctt_netadapter_counters   Counters;
    // Registered memory and packet slots.
    EthernetPool_t                   Pools[ETHERNET_POOLS];
    // Recent requests and their saved admission results.
    EthernetBatch_t                  Batches[ETHERNET_WINDOW];
    // Results kept until the client acknowledges them.
    struct ctt_netadapter_completion Journal[ETHERNET_JOURNAL];
} EthernetSession_t;

/**
 * @brief Session identity and client credentials used to answer repeated closes.
 */
typedef struct EthernetClosedSession {
    // Identity of the session that was closed.
    struct ctt_netadapter_session Identity;
    // Connection that opened that session.
    gracht_conn_t                 Owner;
    // Kernel principal of the client that opened the session.
    uint64_t                      Principal;
} EthernetClosedSession_t;

/**
 * @brief Protocol state allocated at registration and released by EthernetCleanup.
 * Registry access and changes to this state require the adapter lock.
 */
struct EthernetAdapterState {
    element_t                        Header;
    uuid_t                           DeviceId;
    bool                             Registered;
    // Also set when Prepare failed and Stop is still required for cleanup.
    bool                             QueuesPrepared;
    struct ctt_netadapter_info       Info;
    struct ctt_netadapter_link       Link;
    EthernetSession_t                Session;
    EthernetClosedSession_t*         Closed;
    size_t                           ClosedCount;
    size_t                           ClosedCapacity;
};

/**
 * @brief Get the module endpoint identity used as the session generation.
 * @return Native server handle. The module server must already be open.
 */
__EXTERN uint64_t
EthernetGeneration(void);

/**
 * @brief Authenticate a session against kernel credentials and its reply endpoint.
 * The caller must hold the adapter lock.
 *
 * @param message Request with kernel-supplied credentials.
 * @param identity Client session token.
 * @return Adapter with the matching active session, or NULL on mismatch.
 */
__EXTERN EthernetDevice_t*
EthernetFindSession(
    _In_ const struct gracht_message*         message,
    _In_ const struct ctt_netadapter_session* identity);

/**
 * @brief Authenticate a repeated close without changing a newer session.
 * The caller must hold the adapter lock.
 *
 * @param message Request with kernel-supplied credentials.
 * @param identity Client session token.
 * @return true if the session and client credentials match a saved close.
 */
__EXTERN bool
EthernetWasClosed(
    _In_ const struct gracht_message*         message,
    _In_ const struct ctt_netadapter_session* identity);

/**
 * @brief Prepare stopped MAC queues. Errors require a stop before cleanup.
 * @param device Adapter protected by the adapter lock.
 * @return MAC prepare status. QueuesPrepared stays set even on failure.
 */
__EXTERN oserr_t
EthernetQueuesPrepare(
    _InOut_ EthernetDevice_t* device);

/**
 * @brief Submit a slot with space reserved for its completion.
 * @param device Adapter protected by the adapter lock.
 * @param slot Accepted slot with reserved completion space.
 * @return OS_EOK if queued, OS_EINPROGRESS if device access is uncertain and
 *         stop is required, or an error confirming that the packet was not queued.
 */
__EXTERN oserr_t
EthernetQueuePacket(
    _In_ EthernetDevice_t* device,
    _In_ EthernetSlot_t*   slot);

/**
 * @brief Send a fault notification after the session has been marked faulted.
 * @param device Active session protected by the adapter lock.
 * @param status Failure to report. A send failure leaves session state unchanged.
 */
__EXTERN void
EthernetSendFault(
    _In_ EthernetDevice_t* device,
    _In_ oserr_t           status);

/**
 * @brief Send the saved link snapshot without changing its sequence.
 * @param device Active session protected by the adapter lock.
 */
__EXTERN void
EthernetSendLink(
    _In_ EthernetDevice_t* device);

/**
 * @brief Attach a client's shared memory as a packet pool.
 *
 * The session must be idle before adding a pool. Each key identifies one pool
 * for the whole session.
 * Registering the same key and layout again returns its existing ID. Changing
 * the layout is an error. Pool memory must not overlap another registered pool,
 * including memory attached through a different handle.
 *
 * The key and slot sequence numbers stay reserved until the session closes,
 * even after unregistering the pool. On failure, *idOut is zero. If cleanup
 * also fails, the pool stays unavailable to clients and cleanup is retried on
 * the next registration or session close.
 *
 * @param device Device with the session receiving the pool.
 * @param key Client-chosen identifier that must remain unique in this session.
 * @param description Direction and memory layout of the packet pool.
 * @param idOut Receives the pool ID on success, or zero on failure.
 * @return OS_EOK on success, or an error if the pool cannot be registered.
 */
__EXTERN oserr_t
EthernetPoolRegister(
    _InOut_ EthernetDevice_t*                 device,
    _In_    uint64_t                          key,
    _In_    const struct ctt_netadapter_pool* description,
    _Out_   uint32_t*                         idOut);

/**
 * @brief Unregister a pool while the session is idle.
 *
 * The pool's key and per-slot submission sequence history remain reserved until
 * session close, so an old packet identifier cannot later refer to new memory.
 * A cleanup failure disables the pool and keeps the remaining resources for a
 * repeated unregister or session close.
 *
 * @param device Device whose session contains the pool.
 * @param id ID returned when the pool was registered.
 * @return OS_EOK on success, or an error if the pool cannot be unregistered.
 */
__EXTERN oserr_t
EthernetPoolUnregister(
    _InOut_ EthernetDevice_t* device,
    _In_    uint32_t          id);

/**
 * @brief Detach all registered pools after the device has stopped using their memory.
 *
 * Requires a successful queue reset. Releases attached SHM handles,
 * including pools left after registration and cleanup both failed.
 * If a release fails, the remaining resources stay available for another attempt.
 *
 * @param device Device whose session pools are being detached.
 * @return OS_EOK after all pools are released, otherwise a cleanup error.
 */
__EXTERN oserr_t
EthernetPoolsDetach(
    _InOut_ EthernetDevice_t* device);

/**
 * @brief Send saved completion records newer than the client's known sequence.
 *
 * Records stay saved after sending, including when delivery fails. They are
 * removed only after a valid cumulative acknowledgement.
 *
 * @param device Device whose session contains the records.
 * @param after Sequence saved before polling or resetting queues. Must not
 *              precede the retired completion sequence.
 */
__EXTERN void
EthernetPushCompletions(
    _In_ EthernetDevice_t* device,
    _In_ uint64_t          after);

/**
 * @brief Copy saved completion records after an unretired sequence.
 *
 * The caller must hold the adapter lock. Selection does not acknowledge records
 * or send events. The same selection is used for unsolicited pushes and drains.
 *
 * @param device Session containing the journal.
 * @param after Exclusive sequence boundary, between retired and highest progress.
 * @param limit Maximum records to copy, from one through ETHERNET_BATCH.
 * @param records Output storage for at least limit records.
 * @param countOut Number copied, or zero on error.
 * @return OS_EOK, OS_ENOENT for a retired boundary, or OS_EINVALPARAMS.
 */
__EXTERN oserr_t
EthernetReadCompletions(
    _In_  const EthernetDevice_t*           device,
    _In_  uint64_t                          after,
    _In_  uint32_t                          limit,
    _Out_ struct ctt_netadapter_completion* records,
    _Out_ uint32_t*                         countOut);

/**
 * @brief Poll, acknowledge progress and select records for a new drain request.
 *
 * The caller must hold the adapter lock. Polling may push new completions; the
 * caller sends the selected records and drain result after this operation.
 *
 * @param device Authenticated session receiving the drain request.
 * @param request Increasing request identifier. Older IDs are ignored.
 * @param after Exclusive completion sequence boundary.
 * @param limit Maximum number of completion records requested.
 * @param ack Cumulative progress applied before selecting records.
 * @param result Selection and status for the reply. Untouched for ignored IDs.
 * @return true if the request needs a reply, false if its ID was ignored.
 */
__EXTERN bool
EthernetDrain(
    _InOut_ EthernetDevice_t*               device,
    _In_    uint64_t                        request,
    _In_    uint64_t                        after,
    _In_    uint32_t                        limit,
    _In_    const struct ctt_netadapter_ack* ack,
    _Out_   EthernetDrain_t*                result);

/**
 * @brief Validate and apply the client's cumulative acknowledgement.
 *
 * Both acknowledged positions are checked before either one is advanced. A
 * position older than the saved one does not move it backwards.
 *
 * @param device Device whose session is being acknowledged.
 * @param ack Batch and completion positions the client has processed.
 * @return OS_EOK if accepted, or OS_EINVALPARAMS if either position is ahead
 *         of work the device has accepted or completed.
 */
__EXTERN oserr_t
EthernetAcknowledge(
    _InOut_ EthernetDevice_t*                device,
    _In_    const struct ctt_netadapter_ack* ack);

/**
 * @brief Apply acknowledgement and batch admission without sending a reply.
 *
 * The caller must hold the adapter lock. New requests save each packet result;
 * matching retries return the saved admissions without resubmitting packets.
 *
 * @param device Authenticated session receiving the batch.
 * @param run Current run identifier.
 * @param batchId Ordered batch identifier.
 * @param ack Cumulative progress to validate before processing the batch.
 * @param packets Packet descriptions in this request.
 * @param count Number of packet descriptions.
 * @param direction Receive or transmit direction for the entire batch.
 * @param admissionsOut Saved admissions on success, otherwise NULL. The caller
 *                      must use them before releasing the adapter lock.
 * @return OS_EOK if the batch was consumed or replayed, otherwise a batch error.
 */
__EXTERN oserr_t
EthernetAdmitBatch(
    _InOut_ EthernetDevice_t*                      device,
    _In_    uint64_t                               run,
    _In_    uint64_t                               batchId,
    _In_    const struct ctt_netadapter_ack*        ack,
    _In_    const struct ctt_netadapter_packet*     packets,
    _In_    uint32_t                               count,
    _In_    enum ctt_netadapter_direction          direction,
    _Out_   const struct ctt_netadapter_admission** admissionsOut);

/**
 * @brief Process a batch of packet requests or return the saved result of a retry.
 *
 * New batch IDs must arrive in order. Retrying an existing ID is allowed only
 * when the run, direction, count, and every packet description match the saved
 * request. Each accepted packet reserves space for its eventual completion
 * before it is submitted, so retries cannot submit the same memory a second
 * time or change the original result.
 *
 * @param device Device receiving the request.
 * @param run Current device-run identifier.
 * @param batchId Ordered request identifier, also used to recognize retries.
 * @param ack Client progress positions to validate before processing the batch.
 * @param packets Packet descriptions in this request.
 * @param count Number of entries in packets. Must not exceed ETHERNET_BATCH.
 * @param direction Whether these packets are for receive or transmit.
 */
__EXTERN void
EthernetSubmit(
    _InOut_ EthernetDevice_t*                   device,
    _In_    uint64_t                            run,
    _In_    uint64_t                            batchId,
    _In_    const struct ctt_netadapter_ack*    ack,
    _In_    const struct ctt_netadapter_packet* packets,
    _In_    uint32_t                            count,
    _In_    enum ctt_netadapter_direction       direction);

/**
 * @brief Open a session or return the existing session for the same client.
 *
 * The caller must hold the adapter lock and authenticate the kernel principal.
 * Reserve close bookkeeping and register the reply endpoint before publishing
 * a new identity. Closed endpoints cannot open another session.
 *
 * @param device Adapter selected by the authenticated request.
 * @param message Request supplying the client endpoint and kernel principal.
 * @param features Requested optional features. Only zero is supported.
 * @return OS_EOK with Session.Identity and Link ready for the reply, or an error.
 */
__EXTERN oserr_t
EthernetOpen(
    _InOut_ EthernetDevice_t*            device,
    _In_    const struct gracht_message* message,
    _In_    uint64_t                     features);

/**
 * @brief Configure an idle session with the supported MTU and receive filter.
 * @param device Authenticated adapter protected by the adapter lock.
 * @param mtu Requested Ethernet payload size in bytes.
 * @param filter Requested receive filter flags.
 * @return OS_EOK, OS_EBUSY if not idle, or OS_ENOTSUPPORTED for unsupported values.
 */
__EXTERN oserr_t
EthernetConfigure(
    _InOut_ EthernetDevice_t* device,
    _In_    uint32_t          mtu,
    _In_    uint32_t          filter);

/**
 * @brief Prepare the next run after earlier work has been acknowledged.
 *
 * Requires a configured, idle session with mapped RX and TX pools. A retry of
 * the current prepared or running run succeeds without preparing queues again.
 *
 * @param device Authenticated adapter protected by the adapter lock.
 * @param run Next run identifier, or the current identifier for a retry.
 * @return OS_EOK on success, otherwise a state, pool or MAC prepare error.
 */
__EXTERN oserr_t
EthernetPrepare(
    _InOut_ EthernetDevice_t* device,
    _In_    uint64_t          run);

/**
 * @brief Start a prepared run once enough receive slots have been posted.
 *
 * The fence must match the last consumed batch. A running run accepts a retry
 * only with its original start fence. MAC start failure faults the session.
 *
 * @param device Authenticated adapter protected by the adapter lock.
 * @param run Current run identifier.
 * @param fence Last batch boundary supplied by the client.
 * @return OS_EOK on success, otherwise a state, fence or MAC start error.
 */
__EXTERN oserr_t
EthernetStart(
    _InOut_ EthernetDevice_t* device,
    _In_    uint64_t          run,
    _In_    uint64_t          fence);

/**
 * @brief Stop a device run and prevent further packets from being accepted.
 *
 * The stop boundary is saved before reset is attempted. If reset fails, the
 * session stays closed to new work and memory still in use stays allocated.
 *
 * @param device Device to stop.
 * @param run Run identifier that must match the active run.
 * @param fence Last batch boundary supplied by the client for this run.
 * @return OS_EOK on success, or an error if stopping or reset fails.
 */
__EXTERN oserr_t
EthernetStop(
    _InOut_ EthernetDevice_t* device,
    _In_    uint64_t          run,
    _In_    uint64_t          fence);

/**
 * @brief Close the active session after the device has stopped using its memory.
 *
 * Reset must succeed before queues and pools can be released. A saved close
 * record lets the client retry the request. If reset fails, the session stays
 * closed to new requests and we keep its queues and pools because the device
 * may still be using them.
 *
 * @param device Device whose active session is being closed.
 * @return OS_EOK on success, otherwise a reset or cleanup error.
 */
__EXTERN oserr_t
EthernetClose(
    _InOut_ EthernetDevice_t* device);

/**
 * @brief Check whether pools and completion state can safely be changed.
 *
 * A session is idle only when it is opened or stopped, no packets are waiting
 * for results, and every saved completion has been acknowledged.
 *
 * @param device Device to check.
 * @return true if the session is idle, otherwise false.
 */
__EXTERN bool
EthernetIsIdle(
    _In_ const EthernetDevice_t* device);

#endif //!__ETHERNET_NETADAPTER_PRIVATE_H__

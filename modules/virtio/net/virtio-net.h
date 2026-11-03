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
 * Hold VirtioNetLock when calling these internal functions, except from the
 * fast interrupt handler. Device creation and destruction also require the
 * registry lock. Device and packet slot pointers must not be used after the
 * lock protecting them is released.
 *
 * The module lock serializes protocol requests and deferred interrupt work.
 * The fast handler acknowledges the interrupt and wakes the executor, which
 * processes packets. Reset must succeed before shared packet memory can be
 * detached, because the hardware may still be using it.
 */

#ifndef __VIRTIO_NET_H__
#define __VIRTIO_NET_H__

#include <ddk/interrupts/interrupt.h>
#include <ds/list.h>
#include <os/shm.h>
#include <os/handle.h>
#include <stdatomic.h>
#include <virtio/virtio.h>

#include <ctt_netadapter_service_server.h>

// Forward declarations
typedef struct VirtioNetDevice VirtioNetDevice_t;
typedef struct VirtioNetPool   VirtioNetPool_t;

// Device feature bit for reporting its hardware address (MAC address).
#define VIRTIO_NET_F_MAC (1ULL << 5)
// Device feature bit for reporting link state.
#define VIRTIO_NET_F_STATUS (1ULL << 16)
// Largest Ethernet payload supported by this adapter, in bytes.
#define VIRTIO_NET_MTU 1500
// Largest Ethernet frame supported, including its 14-byte Ethernet header.
#define VIRTIO_NET_FRAME_SIZE 1514
// Maximum number of packet pools that can be registered in one session.
#define VIRTIO_NET_POOLS 4
// Maximum number of packet slots in one pool.
#define VIRTIO_NET_SLOTS 32
// Maximum number of packets accepted in one submit request.
#define VIRTIO_NET_BATCH 16
// Number of recent submit results kept so a client can safely retry a request.
#define VIRTIO_NET_WINDOW 4
// Number of completion records kept until the client acknowledges them.
#define VIRTIO_NET_JOURNAL 128
// Number of entries requested for each device packet queue.
#define VIRTIO_NET_QUEUE_SIZE 128
// Maximum shared-memory capacity accepted for one packet pool.
#define VIRTIO_NET_POOL_BYTES (128 * 1024)
// Bytes reserved per slot for the device header and zero-filled frame padding.
#define VIRTIO_NET_METADATA_STRIDE 64
// Initial allocation for closed sessions remembered for retry handling.
#define VIRTIO_NET_INITIAL_CLOSED_SESSIONS 16

/** 
 * @brief Header placed before each frame in device-visible packet memory.
 *
 * Modern Virtio always includes NumBuffers. For legacy Virtio, that field
 * requires VIRTIO_NET_F_MRG_RXBUF. This driver uses the modern header and
 * negotiates MAC/STATUS, without merged receive frames or offload support.
 * Transmit offload fields are zero. Each received frame uses one buffer.
 */
PACKED_TYPESTRUCT(VirtioNetHeader, {
    // Checksum/offload flags. Zero for transmit.
    uint8_t  Flags;
    // Segmentation type. Zero when offload is unused.
    uint8_t  GsoType;
    // Header length used by segmentation offload.
    uint16_t HeaderLength;
    // Segment size used by segmentation offload.
    uint16_t GsoSize;
    // Byte offset where checksum calculation begins.
    uint16_t ChecksumStart;
    // Byte offset of the checksum field.
    uint16_t ChecksumOffset;
    // Number of buffers in this frame. One for normal RX, zero for TX.
    uint16_t NumBuffers;
});

_Static_assert(sizeof(VirtioNetHeader_t) == 12, "modern Virtio network header");

/**
 * @brief Packet description, sequence and DMA state for one pool slot.
 */
typedef struct VirtioNetSlot {
    // Pool containing this slot. Valid while the pool is attached.
    VirtioNetPool_t*             Pool;
    // Most recently accepted packet description for this slot.
    struct ctt_netadapter_packet Packet;
    // Last used submission sequence, including rejected requests.
    uint64_t                     Sequence;
    // Offset of this slot's private header area in device metadata.
    uint32_t                     MetadataOffset;
    // True while a queued packet still needs a completion.
    bool                         Owned;
    // Mapped at pool registration. Each slot can start and end DMA separately.
    struct DMAMapping*           Mapping;
    // True between a successful DMA begin and end.
    bool                         DmaActive;
} VirtioNetSlot_t;

/** Shared memory registered by a client as a group of packet slots. */
struct VirtioNetPool {
    // Attached client shared memory.
    OSHandle_t                 Memory;
    // CPU physical ranges used to reject overlapping pools, not to build descriptors.
    SHMSGTable_t               AliasGeometry;
    // Fixed direction and layout supplied at registration.
    struct ctt_netadapter_pool Description;
    // Client key kept reserved until the session closes.
    uint64_t                   Registration;
    // Whether Memory and slot mappings are ready for packet use.
    bool                       Mapped;
    // Per-slot sequence and in-flight packet state.
    VirtioNetSlot_t            Slots[VIRTIO_NET_SLOTS];
};

/** Cached result of one submit request, used to recognize safe retries. */
typedef struct VirtioNetBatch {
    // Client-provided identifier for this request.
    uint64_t                        Id;
    // Device run in which this request was received.
    uint64_t                        Run;
    // Direction shared by all packets in the request.
    enum ctt_netadapter_direction   Direction;
    // Number of packet entries saved below.
    uint32_t                        Count;
    // Original entries used to validate retries.
    struct ctt_netadapter_packet    Packets[VIRTIO_NET_BATCH];
    // Saved per-packet results returned on retry.
    struct ctt_netadapter_admission Admissions[VIRTIO_NET_BATCH];
} VirtioNetBatch_t;

enum VirtioNetState {
    // Session exists, but queues and packet pools are not ready for work.
    VIRTIO_NET_OPENED,
    // Queues are ready. Receive buffers may be submitted before start.
    VIRTIO_NET_PREPARED,
    // Device is active and may process transmit and receive packets.
    VIRTIO_NET_RUNNING,
    // New work is blocked while the device is being reset.
    VIRTIO_NET_STOPPING,
    // Device is inactive. Acknowledged work may be prepared for another run.
    VIRTIO_NET_STOPPED
};

/**
 * @brief Client session, packet pools, and saved request and completion results.
 */
typedef struct VirtioNetSession {
    // Whether a client has an active session.
    bool                             Active;
    // Whether the device configuration has been read for this session.
    bool                             Configured;
    // Whether new packet requests are blocked after a device error.
    bool                             Faulted;
    // Connection that opened the session.
    gracht_conn_t                    Owner;
    // Token clients must present to identify this session.
    struct ctt_netadapter_session    Identity;
    // Current session state.
    enum VirtioNetState              State;
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
    // Accepted packets awaiting results, indexed by direction.
    uint32_t                         Outstanding[2];
    // Accepted, completed, and acknowledged sequence positions.
    struct ctt_netadapter_progress   Progress;
    // Packet, byte, error, and queue-full totals.
    struct ctt_netadapter_counters   Counters;
    // Registered memory and packet slots.
    VirtioNetPool_t                  Pools[VIRTIO_NET_POOLS];
    // Recent requests and their saved admission results.
    VirtioNetBatch_t                 Batches[VIRTIO_NET_WINDOW];
    // Results kept until the client acknowledges them.
    struct ctt_netadapter_completion Journal[VIRTIO_NET_JOURNAL];
} VirtioNetSession_t;

// Used to identify repeated closes of the same session.
typedef struct VirtioNetClosedSession {
    // Identity of the session that was closed.
    struct ctt_netadapter_session Identity;
    // Connection that opened that session.
    gracht_conn_t                 Owner;
} VirtioNetClosedSession_t;

// Interrupt data shared between the fast handler and the executor.
typedef struct VirtioNetInterruptResource {
    // Register offset used to acknowledge the device interrupt.
    uint32_t            IsrOffset;
    // MSI-X shares vector zero between queues and configuration changes.
    int                 MsixEnabled;
    // Interrupt bits saved for the executor to process.
    atomic_uint_fast8_t PendingStatus;
} VirtioNetInterruptResource_t;

// Device, queues, interrupt resources, and the active or recently closed sessions.
struct VirtioNetDevice {
    // Registry entry used to find this device.
    element_t                    Header;
    // Discovered bus descriptor, freed when this device is destroyed.
    BusDevice_t*                 BusDevice;
    // PCI access and device-reset state.
    VirtioPciTransport_t         Transport;
    // Queue where writable receive buffers are submitted.
    VirtioSplitQueue_t*          ReceiveQueue;
    // Queue where frames to send are submitted.
    VirtioSplitQueue_t*          TransmitQueue;
    // DMA memory for Virtio headers and zero-filled frame padding.
    struct DMABuffer*            Metadata;
    // Device-visible segments for Metadata.
    const struct DMASegment*     MetadataSegments;
    uint32_t                     MetadataSegmentCount;
    // Features successfully negotiated with the device.
    uint64_t                     Features;
    // Adapter information reported to clients.
    struct ctt_netadapter_info   Info;
    // Latest known link state reported to clients.
    struct ctt_netadapter_link   Link;
    // State passed between interrupt handling stages.
    VirtioNetInterruptResource_t InterruptResource;
    // Registered interrupt identifier, when present.
    DeviceInterrupt_t            Interrupt;
    // Executor event used to process deferred interrupts.
    int                          EventDescriptor;
    // Current client session and its packet state.
    VirtioNetSession_t           Session;
    // Recent close records for retry handling.
    VirtioNetClosedSession_t*    Closed;
    // Number of valid entries in Closed.
    size_t                       ClosedCount;
    // Number of allocated entries in Closed.
    size_t                       ClosedCapacity;
};

/**
 * @brief Free a converted bus descriptor and its separately allocated identification.
 * 
 * @param busDevice Descriptor to release. NULL is allowed.
 */
__EXTERN void
VirtioNetBusDeviceDestroy(
    _In_ BusDevice_t* busDevice);

/** 
 * @brief Create and initialize a device from a discovered bus descriptor.
 *
 * The caller must not free busDevice after this call, including on failure.
 * The driver releases it during cleanup. If reset or resource release fails,
 * the device stays on a private cleanup list for another attempt and cannot
 * be found by clients.
 * 
 * @param busDevice Discovered bus descriptor to initialize.
 * @return The initialized device, or NULL if setup fails.
 */
__EXTERN VirtioNetDevice_t*
VirtioNetDeviceCreate(
    _In_ BusDevice_t* busDevice);

/**
 * @brief Resolve a metadata cell to its device-visible address.
 *
 * Each cell must fit within one DMA segment so its header and padding can be
 * described to the device. The full allocation may span several segments.
 *
 * @param device Device containing the metadata allocation.
 * @param offset Byte offset of the cell within the allocation.
 * @param addressOut Device-visible start of the cell on success.
 * @return OS_EOK, or an error if the cell is absent or crosses a segment.
 */
__EXTERN oserr_t
VirtioNetMetadataAddress(
    _In_  VirtioNetDevice_t* device,
    _In_  size_t             offset,
    _Out_ uint64_t*         addressOut);

/**
 * @brief Stop and release a device whose queues are no longer in use.
 *
 * If reset fails, the device may still use its queues or shared memory.
 * The caller must keep the device and its resources allocated for another
 * cleanup attempt.
 * 
 * @param device Device to destroy.
 * @return OS_EOK if released, otherwise a reset or cleanup error. On error, the
 *         caller must keep the device for another cleanup attempt.
 */
__EXTERN oserr_t
VirtioNetDeviceDestroy(
    _In_ VirtioNetDevice_t* device);

/**
 * @brief Keep a device on the private cleanup list after destruction fails.
 *
 * The caller must hold the module lock, and the device must not already be on
 * the list. Devices on this list cannot be found by device or session lookup.
 *
 * @param device Device whose cleanup needs another attempt.
 */
__EXTERN void
VirtioNetRetainDevice(
    _In_ VirtioNetDevice_t* device);

/**
 * @brief Read a consistent snapshot of the device address and link state.
 *
 * Sends a notification if the link state changed. Clients can call get_link
 * to read the current state if they miss a notification.
 * 
 * @param device Device to read.
 * @return OS_EOK on success, or the error that prevented a consistent read.
 */
__EXTERN oserr_t
VirtioNetReadConfiguration(
    _In_ VirtioNetDevice_t* device);

/**
 * @brief Acquire the module lock before accessing device registry or session state.
 */
__EXTERN void
VirtioNetLock(void);

/**
 * @brief Release the module lock after registry or session state has been accessed.
 */
__EXTERN void
VirtioNetUnlock(void);

/**
 * @brief Find a registered device by its identifier.
 * 
 * @param deviceId Identifier assigned to the device.
 * @return A temporary device pointer, or NULL if no device matches. The caller
 *         must keep the registry lock held while using the returned pointer.
 */
__EXTERN VirtioNetDevice_t*
VirtioNetFindDevice(
    _In_ uuid_t deviceId);

/**
 * @brief Find the active session matching the request connection and session token.
 * 
 * @param message Request whose connection identifies the client.
 * @param identity Session token supplied by that client.
 * @return A temporary device pointer, or NULL if the request does not match an
 *         active session. Keep the module and registry locks held while using it.
 */
__EXTERN VirtioNetDevice_t*
VirtioNetFindSession(
    _In_ const struct gracht_message*         message,
    _In_ const struct ctt_netadapter_session* identity);

/**
 * @brief Check whether a request matches a recently closed session.
 *
 * This lets a client retry a close request without attaching memory or changing
 * device state.
 * 
 * @param message Request whose connection identifies the client.
 * @param identity Session token supplied by that client.
 * @return true if this connection and token identify a remembered close.
 */
__EXTERN bool
VirtioNetWasClosed(
    _In_ const struct gracht_message*         message,
    _In_ const struct ctt_netadapter_session* identity);

/**
 * @brief Return the server used to send network-adapter messages and events.
 */
__EXTERN gracht_server_t*
VirtioNetServer(void);

/**
 * @brief Return the current generation used to distinguish device discoveries.
 */
__EXTERN uint64_t
VirtioNetGeneration(void);

/**
 * @brief Register the device interrupt and the executor event used to process it.
 *
 * The fast interrupt handler only acknowledges the interrupt and signals the
 * executor. The executor later handles packet completions and protocol work
 * while holding the module lock.
 * 
 * @param device Device whose interrupt is being registered.
 * @return OS_EOK on success, or an error if registration fails.
 */
__EXTERN oserr_t
VirtioNetRegisterInterrupt(
    _In_ VirtioNetDevice_t* device);

/**
 * @brief Disable and release the device interrupt and its executor event.
 */
__EXTERN oserr_t
VirtioNetUnregisterInterrupt(
    _In_ VirtioNetDevice_t* device);

/**
 * @brief Acknowledge the interrupt and wake the executor from the fast handler.
 * 
 * @param functionTable Interrupt function table provided by the kernel.
 * @param resourceTable Interrupt resource table provided by the kernel.
 * @return Interrupt handling result required by the kernel.
 */
__EXTERN irqstatus_t
OnFastInterrupt(
    _In_ InterruptFunctionTable_t*,
    _In_ InterruptResourceTable_t*);

/**
 * @brief Prepare the packet queues without allowing the device to process packets.
 *
 * Creates queues using the negotiated features. The device stays stopped
 * until the session starts and marks it ready. Before destroying queues or
 * cancelling packets, reset must succeed so the device has stopped using
 * their memory.
 * 
 * @param device Device whose queues are being prepared.
 * @return OS_EOK on success, or the first error that prevents setup.
 */
__EXTERN oserr_t
VirtioNetQueuesPrepare(
    _In_ VirtioNetDevice_t* device);

/**
 * @brief Reset the device, cancel queued packets, and destroy both packet queues.
 *
 * Queue memory and packet buffers are only released after reset succeeds. If
 * reset fails, the queues and their memory must stay allocated.
 * 
 * @param device Device whose queues are being reset.
 * @return OS_EOK on success, or an error if reset or queue cleanup fails.
 */
__EXTERN oserr_t
VirtioNetQueuesReset(
    _In_ VirtioNetDevice_t* device);

/**
 * @brief Submit a reserved packet slot to the receive or transmit queue.
 *
 * The caller must reserve the slot and its completion space first. OS_EOK means
 * the queue accepted the packet. On OS_EINPROGRESS, keep the packet accepted
 * because notification or DMA cleanup failed. Treat the device as faulted and
 * reset it to finish cleanup. On any other error, the caller may undo the
 * reservation.
 * 
 * @param device Device that will process the packet.
 * @param slot Reserved packet slot to submit.
 * @return OS_EOK if submitted, OS_EINPROGRESS if recovery is needed, otherwise
 *         the submission error.
 */
__EXTERN oserr_t
VirtioNetQueuePacket(
    _In_ VirtioNetDevice_t* device,
    _In_ VirtioNetSlot_t*   slot);

/**
 * @brief Process completed queue entries and send any resulting completion records.
 *
 * Each call limits the number of completions it processes. Call after an
 * interrupt, or during drain recovery if the interrupt was missed.
 * 
 * @param device Device whose queues are checked.
 */
__EXTERN void
VirtioNetPoll(
    _In_ VirtioNetDevice_t* device);

/**
 * @brief Stop accepting new packets and notify the client about a device fault.
 *
 * The device is not reset here. Keep packet memory allocated because the
 * queues may still be using it.
 * 
 * @param device Faulted device.
 * @param status Error reported to the client.
 */
__EXTERN void
VirtioNetFault(
    _In_ VirtioNetDevice_t* device,
    _In_ oserr_t            status);

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
VirtioNetPoolRegister(
    _In_ VirtioNetDevice_t*                device,
    _In_ uint64_t                          key,
    _In_ const struct ctt_netadapter_pool* description,
    _Out_ uint32_t*                         idOut);

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
VirtioNetPoolUnregister(
    _In_ VirtioNetDevice_t* device,
    _In_ uint32_t           id);

/**
 * @brief Detach all registered pools after the device has stopped using their memory.
 *
 * Requires a successful queue reset. Releases DMA mappings and attached
 * handles, including pools left after registration and cleanup both failed.
 * If a release fails, the remaining resources stay available for another attempt.
 * 
 * @param device Device whose session pools are being detached.
 * @return OS_EOK after all pools are released, otherwise a cleanup error.
 */
__EXTERN oserr_t
VirtioNetPoolsDetach(
    _In_ VirtioNetDevice_t* device);

/**
 * @brief Get the CPU address of a packet slot after its DMA transfer has ended.
 *
 * The DMA transfer must have ended before inspecting received bytes. The
 * address is valid only while the slot's pool is attached.
 * 
 * @param slot Slot whose packet bytes are requested.
 * @return Address of the slot's packet data, or NULL if it is not available.
 */
__EXTERN void*
VirtioNetPacketBytes(
    _In_ VirtioNetSlot_t* slot);

/**
 * @brief Record the one final result for a packet and release its slot for reuse.
 *
 * A completion entry was reserved when the packet was accepted, so this can
 * also record cancellations during reset without allocating memory. A slot
 * that has already received a result is ignored.
 * 
 * @param device Device whose session contains the packet.
 * @param slot Packet slot being completed.
 * @param status Final packet result.
 * @param length Number of frame bytes transferred on success. Ignored on error.
 */
__EXTERN void
VirtioNetComplete(
    _In_ VirtioNetDevice_t* device,
    _In_ VirtioNetSlot_t*   slot,
    _In_ oserr_t            status,
    _In_ uint32_t           length);

/**
 * @brief Send saved completion records newer than the client's known sequence.
 *
 * Records stay saved after sending, including when delivery fails. They are
 * removed only after a valid cumulative acknowledgement.
 * 
 * @param device Device whose session contains the records.
 * @param after Sequence number the client has already received.
 */
__EXTERN void
VirtioNetPushCompletions(
    _In_ VirtioNetDevice_t* device,
    _In_ uint64_t           after);

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
VirtioNetAcknowledge(
    _In_ VirtioNetDevice_t*               device,
    _In_ const struct ctt_netadapter_ack* ack);

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
 * @param count Number of entries in packets. Must not exceed VIRTIO_NET_BATCH.
 * @param direction Whether these packets are for receive or transmit.
 */
__EXTERN void
VirtioNetSubmit(
    _In_ VirtioNetDevice_t*                  device,
    _In_ uint64_t                            run,
    _In_ uint64_t                            batchId,
    _In_ const struct ctt_netadapter_ack*    ack,
    _In_ const struct ctt_netadapter_packet* packets,
    _In_ uint32_t                            count,
    _In_ enum ctt_netadapter_direction       direction);

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
VirtioNetStop(
    _In_ VirtioNetDevice_t* device,
    _In_ uint64_t           run,
    _In_ uint64_t           fence);

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
VirtioNetClose(
    _In_ VirtioNetDevice_t* device);

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
VirtioNetIsIdle(
    _In_ const VirtioNetDevice_t* device);

#endif

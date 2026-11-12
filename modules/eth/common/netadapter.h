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

#ifndef __ETHERNET_NETADAPTER_H__
#define __ETHERNET_NETADAPTER_H__

#include <gracht/server.h>

#include "phy.h"

// Packet slots and protocol state are private to the common adapter code.
DECL_STRUCT(EthernetDevice);
DECL_STRUCT(EthernetSlot);
struct EthernetAdapterState;

// Largest Ethernet payload supported by this adapter, in bytes.
#define ETHERNET_MTU         1500
#define ETHERNET_HEADER_SIZE 14U
#define ETHERNET_FCS_SIZE    4U

// Minimum transmitted frame, excluding the FCS appended by the MAC.
#define ETHERNET_MIN_FRAME_SIZE (64U - ETHERNET_FCS_SIZE)

// Largest frame: 1500 payload bytes and a 14-byte header. Excludes the FCS.
// VLAN tags and jumbo frames are not supported by this configuration.
#define ETHERNET_FRAME_SIZE (ETHERNET_MTU + ETHERNET_HEADER_SIZE)

/**
 * @brief MAC requirements used when publishing and starting an adapter.
 * Common packet, replay and completion capacities remain fixed framework limits.
 */
struct EthernetRegistrationConfig {
    // Minimum posted RX slots before Start. Zero permits starting without RX slots.
    uint32_t MinRxSlots;
};

/**
 * @brief Hardware operations called with the adapter lock held.
 * All callbacks are required. The callbacks must not take the adapter lock again.
 */
struct EthernetOperations {
    // Prepare may leave partial state on failure. Stop must handle that state.
    oserr_t (*Prepare)(EthernetDevice_t* device);
    oserr_t (*Start)(EthernetDevice_t* device);
    // OS_EOK confirms that hardware can no longer access packet storage and
    // the MAC has cleared all slot references.
    oserr_t (*Stop)(EthernetDevice_t* device);
    // The slot is valid until completion or successful Stop. OS_EINPROGRESS
    // means hardware may have seen the packet. Other errors
    // must leave it unused so the common code can cancel its admission.
    oserr_t (*Submit)(EthernetDevice_t* device, EthernetSlot_t* slot);
    // Fill the complete link on success. Output is ignored on failure.
    oserr_t (*ReadLink)(EthernetDevice_t* device, struct PhyLink* link);
    // Process bounded work and report finished slots through EthernetComplete.
    void (*Poll)(EthernetDevice_t* device);
};

/**
 * @brief MAC callbacks and context for one adapter, with private common state.
 * Initialize Operations and Controller in zeroed storage before registration.
 * Common is managed only by the common adapter code. Hold the adapter lock
 * when calling adapter operations or accessing accepted packet slots.
 */
struct EthernetDevice {
    const struct EthernetOperations* Operations;
    void*                            Controller;
    struct EthernetAdapterState*     Common;
};

/**
 * @brief Reason a received frame was discarded without completing a slot.
 */
enum EthernetReceiveDrop {
    // Destination does not match the configured receive filter.
    EthernetReceiveFiltered,
    // No receive slot is available for the frame.
    EthernetReceiveNoBuffer,
    // Hardware reported an invalid length or a frame error.
    EthernetReceiveError
};

/**
 * @brief Serialize registry, protocol, PHY and MAC work.
 */
__EXTERN void
EthernetLock(void);

/**
 * @brief Release the adapter lock held by the current executor.
 */
__EXTERN void
EthernetUnlock(void);

/**
 * @brief Get the module server used for directed adapter events.
 * @return Module server initialized by the module runtime.
 */
__EXTERN gracht_server_t*
EthernetServer(void);

/**
 * @brief Find a published adapter while holding the adapter lock.
 * @param deviceId Device identifier assigned by deviced.
 * @return Published adapter, or NULL if deviceId is not registered.
 */
__EXTERN EthernetDevice_t*
EthernetFindDevice(
    _In_ uuid_t deviceId);

/**
 * @brief Initialize adapter locking, protocol registration and bounded event sends.
 * Call once after the module server has been created, before publishing adapters.
 * @return OS_EOK, or OS_EUNKNOWN if protocol registration fails.
 */
__EXTERN oserr_t
EthernetServiceInitialize(void);

/**
 * @brief Allocate common state and publish an adapter with a valid unicast MAC.
 * The caller must validate mac and initialize the hardware callbacks first.
 * Common must be NULL. On failure the adapter stays unpublished.
 * @param device Adapter protected by the adapter lock.
 * @param deviceId Device identifier assigned by deviced.
 * @param mac Six-byte firmware or bootloader MAC address.
 * @param config MAC requirements copied during registration. Must be supplied.
 * @return OS_EOK on success, OS_EINVALPARAMS if MinRxSlots exceeds the common
 *         RX slot capacity, or OS_EOOM if common state cannot be allocated.
 */
__EXTERN oserr_t
EthernetRegister(
    _InOut_ EthernetDevice_t*                        device,
    _In_    uuid_t                                   deviceId,
    _In_    const uint8_t*                           mac,
    _In_    const struct EthernetRegistrationConfig* config);

/**
 * @brief Remove an adapter from lookup before beginning destruction.
 * @param device Adapter protected by the adapter lock.
 */
__EXTERN void
EthernetUnregister(
    _In_ EthernetDevice_t* device);

/**
 * @brief Unpublish an adapter and release all common packet and session state.
 *
 * Stops prepared queues before releasing packet storage. The MAC must first
 * stop any hardware initialized outside the Prepare callback. On a stop or
 * memory release error, the adapter stays unpublished and remaining state
 * stays allocated for another cleanup attempt. Success clears Common.
 * Safe before registration, after failed registration, and after cleanup.
 *
 * @param device Adapter protected by the adapter lock.
 * @return OS_EOK after cleanup, otherwise the stop or memory release error.
 */
__EXTERN oserr_t
EthernetCleanup(
    _InOut_ EthernetDevice_t* device);

/**
 * @brief Read the link through the MAC callback and report a changed snapshot.
 * @param device Adapter protected by the adapter lock.
 * @return OS_EOK on success, OS_EOVERFLOW if the link sequence is exhausted,
 *         or the callback error. Errors fault the session and keep the saved link.
 */
__EXTERN oserr_t
EthernetReadConfiguration(
    _InOut_ EthernetDevice_t* device);

/**
 * @brief Confirm MAC DMA stop before completing every queued slot as cancelled.
 * May also be called before registration to stop partially initialized hardware.
 * @param device Adapter protected by the adapter lock.
 * @return OS_EOK after cancellation, or the MAC stop error. Failed stop leaves
 *         packet storage and pending completions unchanged for another attempt.
 */
__EXTERN oserr_t
EthernetQueuesReset(
    _InOut_ EthernetDevice_t* device);

/**
 * @brief Poll bounded MAC work and push newly completed journal entries.
 * @param device Adapter protected by the adapter lock.
 */
__EXTERN void
EthernetPoll(
    _In_ EthernetDevice_t* device);

/**
 * @brief Block new packets and report the first session fault.
 * @param device Adapter protected by the adapter lock.
 * @param status Failure to report.
 */
__EXTERN void
EthernetFault(
    _InOut_ EthernetDevice_t* device,
    _In_    oserr_t           status);

/**
 * @brief Get the CPU address of an accepted packet slot.
 *
 * The MAC copies TX bytes before descriptor publication and RX bytes before
 * completion. The address is valid until completion or successful MAC stop.
 *
 * The caller must hold the adapter lock and supply a slot passed to Submit.
 * Packet bounds have already been checked during admission.
 *
 * @param slot Slot whose packet bytes are requested.
 * @return CPU address of the slot's packet data.
 */
__EXTERN void*
EthernetPacketBytes(
    _In_ EthernetSlot_t* slot);

/**
 * @brief Get the accepted frame length or receive capacity in bytes.
 * @param slot Accepted packet slot protected by the adapter lock.
 * @return TX frame length, or RX capacity available for a received frame.
 */
__EXTERN uint32_t
EthernetPacketLength(
    _In_ const EthernetSlot_t* slot);

/**
 * @brief Check whether an accepted slot is for received frames.
 * @param slot Accepted packet slot protected by the adapter lock.
 * @return true for RX, false for TX.
 */
__EXTERN bool
EthernetPacketIsReceive(
    _In_ const EthernetSlot_t* slot);

/**
 * @brief Count a received frame discarded without completing a packet slot.
 * @param device Registered adapter protected by the adapter lock.
 * @param reason Receive filter, buffer availability, or hardware error result.
 */
__EXTERN void
EthernetReportReceiveDrop(
    _InOut_ EthernetDevice_t*        device,
    _In_    enum EthernetReceiveDrop reason);

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
EthernetComplete(
    _InOut_ EthernetDevice_t* device,
    _InOut_ EthernetSlot_t*   slot,
    _In_    oserr_t           status,
    _In_    uint32_t          length);

#endif //!__ETHERNET_NETADAPTER_H__

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

#include <ddk/utils.h>
#include <os/usched/mutex.h>
#include <internal/_service.h>
#include <internal/_utils.h>
#include <io.h>
#include <stdlib.h>
#include <string.h>

#include "netadapter-private.h"

extern gracht_server_t* __crt_get_module_server(void);
extern int __crt_get_server_iod(void);
extern void __crt_module_set_send_timeout(uint32_t milliseconds);
static list_t g_devices = LIST_INIT;
static struct usched_mtx g_lock;

void
EthernetLock(void)
{
    // Protocol requests and hardware polling share the registry and packet state.
    usched_mtx_lock(&g_lock);
}

void
EthernetUnlock(void)
{
    usched_mtx_unlock(&g_lock);
}

gracht_server_t*
EthernetServer(void)
{
    return __crt_get_module_server();
}

uint64_t
EthernetGeneration(void)
{
    return GetNativeHandle(__crt_get_server_iod());
}

EthernetDevice_t*
EthernetFindDevice(
    _In_ uuid_t deviceId)
{
    return list_find_value(&g_devices, (void*)(uintptr_t)deviceId);
}

EthernetDevice_t*
EthernetFindSession(
    _In_ const struct gracht_message*         message,
    _In_ const struct ctt_netadapter_session* identity)
{
    EthernetDevice_t*  device;
    EthernetSession_t* session;

    // The reply endpoint alone does not authenticate a client. Require the
    // kernel principal as well before comparing the session token.
    if (!__IsAuthenticatedCaller(&message->security)) {
        return NULL;
    }

    foreach (element, &g_devices) {
        device = element->value;
        session = &device->Common->Session;

        if (!session->Active || session->Owner != message->client ||
            session->Principal != message->security.principal) {
            continue;
        }

        if (identity->id == session->Identity.id && identity->generation == session->Identity.generation) {
            return device;
        }
    }
    return NULL;
}

bool
EthernetWasClosed(
    _In_ const struct gracht_message*         message,
    _In_ const struct ctt_netadapter_session* identity)
{
    if (!__IsAuthenticatedCaller(&message->security)) {
        return false;
    }

    foreach (element, &g_devices) {
        EthernetDevice_t* device = element->value;

        // A delayed close must only match its saved session, even if another
        // client has since opened the same adapter.
        for (size_t i = 0; i < device->Common->ClosedCount; i++) {
            EthernetClosedSession_t* closed = &device->Common->Closed[i];
            if (closed->Owner != message->client || closed->Principal != message->security.principal) {
                continue;
            }

            if (identity->id == closed->Identity.id && identity->generation == closed->Identity.generation) {
                return true;
            }
        }
    }
    return false;
}

oserr_t
EthernetServiceInitialize(void)
{
    usched_mtx_init(&g_lock, USCHED_MUTEX_PLAIN);

    // Event sends run with the adapter lock held. Limit each send to 1 ms so
    // a client that stops reading cannot block hardware polling indefinitely.
    __crt_module_set_send_timeout(1);
    if (gracht_server_register_protocol(
        EthernetServer(),
        &ctt_netadapter_server_protocol
    )) {
        ERROR("EthernetServiceInitialize: protocol registration failed");
        return OS_EUNKNOWN;
    }
    return OS_EOK;
}

oserr_t
EthernetRegister(
    _InOut_ EthernetDevice_t*                        device,
    _In_    uuid_t                                   deviceId,
    _In_    const uint8_t*                           mac,
    _In_    const struct EthernetRegistrationConfig* config)
{
    // One RX pool must be able to supply the MAC's startup requirement.
    if (config->MinRxSlots > ETHERNET_SLOTS) {
        return OS_EINVALPARAMS;
    }

    device->Common = calloc(1, sizeof(*device->Common));
    if (device->Common == NULL) {
        return OS_EOOM;
    }

    // The caller has validated the MAC and initialized the hardware callbacks.
    ELEMENT_INIT(&device->Common->Header, 0, device);
    device->Common->Header.key = (void*)(uintptr_t)deviceId;
    device->Common->DeviceId = deviceId;

    // Contract version 2: one port (ID 0), one TX/RX queue pair and one
    // contiguous packet range per submission. Packet copies allow byte alignment.
    device->Common->Info = (struct ctt_netadapter_info) {
        .port_count = 1,
        .min_version = 2,
        .max_version = 2,
        .framing = CTT_NETADAPTER_FRAMING_ETHERNET,
        .medium = CTT_NETADAPTER_MEDIUM_WIRED,
        .min_mtu = ETHERNET_MTU,
        .max_mtu = ETHERNET_MTU,
        .current_mtu = ETHERNET_MTU,
        .max_frame_size = ETHERNET_FRAME_SIZE,
        .buffer_alignment = 1,
        .max_pools = ETHERNET_POOLS,
        .max_pool_bytes = ETHERNET_POOL_BYTES,
        // At most one mapped pool per direction, even when old keys use slots.
        .max_registered_bytes = 2 * ETHERNET_POOL_BYTES,
        .max_slots_per_pool = ETHERNET_SLOTS,
        .max_queue_pairs = 1,
        .max_segments = 1,
        .max_batch_size = ETHERNET_BATCH,
        .max_pending_batches = ETHERNET_WINDOW,
        .max_outstanding_tx = ETHERNET_SLOTS,
        .max_outstanding_rx = ETHERNET_SLOTS,
        .max_unacked_completions = ETHERNET_JOURNAL,
        .min_rx_slots = config->MinRxSlots
    };

    device->Common->Info.current_mac = (struct ctt_netadapter_mac) { mac[0], mac[1], mac[2], mac[3], mac[4], mac[5] };
    device->Common->Info.permanent_mac = device->Common->Info.current_mac;
    // Sequence zero means no link snapshot has been received by the client.
    device->Common->Link.sequence = 1;
    device->Common->Link.status = CTT_NETADAPTER_LINK_STATUS_DOWN;
    list_append(&g_devices, &device->Common->Header);
    device->Common->Registered = true;
    return OS_EOK;
}

void
EthernetUnregister(
    _In_ EthernetDevice_t* device)
{
    if (device->Common == NULL || !device->Common->Registered) {
        return;
    }
    list_remove(&g_devices, &device->Common->Header);
    device->Common->Registered = false;
}

oserr_t
EthernetCleanup(
    _InOut_ EthernetDevice_t* device)
{
    oserr_t status;

    if (device->Common == NULL) {
        return OS_EOK;
    }

    // Remove client access before a fallible stop or partial memory release.
    EthernetUnregister(device);
    if (device->Common->QueuesPrepared) {
        status = EthernetQueuesReset(device);
        if (status != OS_EOK) {
            return status;
        }
    }

    status = EthernetPoolsDetach(device);
    if (status != OS_EOK) {
        return status;
    }

    free(device->Common->Closed);
    free(device->Common);
    device->Common = NULL;
    return OS_EOK;
}

oserr_t
EthernetReadConfiguration(
    _InOut_ EthernetDevice_t* device)
{
    struct PhyLink             link;
    struct ctt_netadapter_link next = { 0 };
    oserr_t                    status;

    status = device->Operations->ReadLink(device, &link);
    if (status != OS_EOK) {
        // Failed callbacks need not fill link. Report the fault and keep the
        // last valid snapshot until a later read succeeds.
        EthernetFault(device, status);
        return status;
    }

    next.status = link.Carrier ? CTT_NETADAPTER_LINK_STATUS_UP : CTT_NETADAPTER_LINK_STATUS_DOWN;
    next.duplex = !link.Carrier ? CTT_NETADAPTER_DUPLEX_UNKNOWN :
        (link.FullDuplex ? CTT_NETADAPTER_DUPLEX_FULL : CTT_NETADAPTER_DUPLEX_HALF);
    // PHY speeds are in Mbit/s. The protocol reports bits per second.
    next.speed_bps = (uint64_t)link.Speed * 1000000;

    if (next.status != device->Common->Link.status || next.duplex != device->Common->Link.duplex ||
        next.speed_bps != device->Common->Link.speed_bps) {

        if (device->Common->Link.sequence == UINT64_MAX) {
            EthernetFault(device, OS_EOVERFLOW);
            return OS_EOVERFLOW;
        }

        next.sequence = device->Common->Link.sequence + 1;
        device->Common->Link = next;

        if (device->Common->Session.Active) {
            // get_link reads the saved snapshot again if this event is lost.
            EthernetSendLink(device);
        }
    }
    return OS_EOK;
}

oserr_t
EthernetQueuesPrepare(
    _InOut_ EthernetDevice_t* device)
{
    oserr_t status;

    // Prepare can fail after allocating queues or touching hardware. Keep this
    // set on failure so teardown still stops the MAC before freeing its memory.
    device->Common->QueuesPrepared = true;
    status = device->Operations->Prepare(device);
    if (status != OS_EOK) {
        ERROR("EthernetQueuesPrepare: device %u failed (%u)", device->Common->DeviceId, status);
    }
    return status;
}

oserr_t
EthernetQueuesReset(
    _InOut_ EthernetDevice_t* device)
{
    oserr_t status;

    status = device->Operations->Stop(device);
    if (status != OS_EOK) {
        // Module teardown may retry on every poll. Report the first failure
        // without filling the log while the device remains unable to stop.
        WARNING_ONCE(ethernet_stop,
            "EthernetQueuesReset: stop failed (%u)", status);
        return status;
    }

    // Creation can fail before common state is registered.
    if (device->Common == NULL) {
        return OS_EOK;
    }

    // Stop has confirmed that hardware can no longer access packet storage.
    // Complete each pending slot once, using the journal space reserved at submit.
    device->Common->QueuesPrepared = false;
    for (uint32_t p = 0; p < device->Common->Session.PoolCount; p++) {
        for (uint32_t s = 0; s < device->Common->Session.Pools[p].Description.slot_count; s++) {
            EthernetComplete(
                device,
                &device->Common->Session.Pools[p].Slots[s],
                OS_ECANCELLED,
                0
            );
        }
    }
    return OS_EOK;
}

oserr_t
EthernetQueuePacket(
    _In_ EthernetDevice_t* device,
    _In_ EthernetSlot_t*   slot)
{
    return device->Operations->Submit(device, slot);
}

void
EthernetPoll(
    _In_ EthernetDevice_t* device)
{
    uint64_t before = device->Common->Session.Progress.highest_completion_sequence;

    if (!device->Common->QueuesPrepared || device->Common->Session.Faulted) {
        return;
    }

    // Only push results produced by this poll. Earlier results remain available
    // through drain until acknowledged, including after a failed event send.
    device->Operations->Poll(device);
    EthernetPushCompletions(device, before);
}

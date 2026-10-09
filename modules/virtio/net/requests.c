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

#include "virtio-net.h"
#include <stdlib.h>
#include <string.h>

static uint64_t g_nextSession = 1;

static oserr_t
__ReserveCloseRecord(
    _InOut_ VirtioNetDevice_t* device)
{
    VirtioNetClosedSession_t* records;
    size_t                    capacity;
    size_t                    maximum = SIZE_MAX / sizeof(*records);

    if (device->ClosedCount < device->ClosedCapacity) {
        return OS_EOK;
    }
    if (device->ClosedCount == maximum) {
        return OS_EOVERFLOW;
    }

    capacity = device->ClosedCapacity;
    if (!capacity) {
        capacity = VIRTIO_NET_INITIAL_CLOSED_SESSIONS;
    } else {
        capacity = capacity > maximum / 2 ? maximum : capacity * 2;
    }

    records = realloc(device->Closed, capacity * sizeof(VirtioNetClosedSession_t));
    if (records == NULL) {
        return OS_EOOM;
    }

    device->Closed = records;
    device->ClosedCapacity = capacity;
    return OS_EOK;
}

void
ctt_netadapter_get_info_invocation(
    struct gracht_message* message,
    uuid_t                 deviceId,
    uint32_t               port)
{
    struct ctt_netadapter_info empty = { 0 };
    VirtioNetDevice_t*         device;
    
    VirtioNetLock();
    device = VirtioNetFindDevice(deviceId);
    ctt_netadapter_get_info_response(
        message,
        device && !port ? OS_EOK : OS_ENOENT,
        device && !port ? &device->Info : &empty
    );
    VirtioNetUnlock();
}

void
ctt_netadapter_open_invocation(
    struct gracht_message* message,
    uuid_t                 deviceId,
    uint32_t               port,
    uint64_t               features)
{
    struct ctt_netadapter_session identity = {0};
    struct ctt_netadapter_link    link = {0};
    oserr_t                       status = OS_EOK;
    VirtioNetDevice_t*            device;
    
    VirtioNetLock();
    device = VirtioNetFindDevice(deviceId);
    if (device == NULL || port) {
        status = OS_ENOENT;
    } else if (features) {
        status = OS_ENOTSUPPORTED;
    } else if (device->Session.Active && device->Session.Owner != message->client) {
        status = OS_EBUSY;
    } else if (!device->Session.Active && g_nextSession == UINT64_MAX) {
        status = OS_EOVERFLOW;
    }

    if (status == OS_EOK && !device->Session.Active) {
        // OPEN has no operation key. Reusing an already closed endpoint would
        // make a delayed old OPEN indistinguishable from a new one. Require a
        // fresh client endpoint and retain close tombstones for retries.
        for (size_t i = 0; i < device->ClosedCount; ++i) {
            if (device->Closed[i].Owner == message->client) {
                status = OS_ENOENT;
            }
        }
    }

    if (status == OS_EOK && !device->Session.Active) {
        status = __ReserveCloseRecord(device);
    }

    if (status == OS_EOK && gracht_server_register_client(message)) {
        status = OS_EOOM;
    }

    if (status == OS_EOK) {
        if (!device->Session.Active) {
            device->Session.Active = true;
            device->Session.Owner = message->client;
            device->Session.Identity =
                (struct ctt_netadapter_session) {
                    g_nextSession++,
                    VirtioNetGeneration()
                };
            device->Session.State = VIRTIO_NET_OPENED;
        }
        identity = device->Session.Identity;
        link = device->Link;
    }

    ctt_netadapter_open_response(message, status, &identity, 0, &link);
    VirtioNetUnlock();
}

void
ctt_netadapter_register_pool_invocation(
    struct gracht_message*               message,
    const struct ctt_netadapter_session* identity,
    uint64_t                             key,
    const struct ctt_netadapter_pool*    pool)
{
    VirtioNetDevice_t* device;
    uint32_t           id = 0;
    oserr_t            status = OS_ENOENT;

    VirtioNetLock();
    device = VirtioNetFindSession(message, identity);
    if (device != NULL) {
        status = VirtioNetPoolRegister(device, key, pool, &id);
    }
    ctt_netadapter_register_pool_response(message, status, id);
    VirtioNetUnlock();
}

void
ctt_netadapter_unregister_pool_invocation(
        struct gracht_message*               message,
        const struct ctt_netadapter_session* identity,
        uint32_t                             id)
{
    VirtioNetDevice_t* device;

    VirtioNetLock();
    device = VirtioNetFindSession(message, identity);
    ctt_netadapter_unregister_pool_response(
        message,
        device ? VirtioNetPoolUnregister(device, id) : OS_ENOENT
    );
    VirtioNetUnlock();
}

void
ctt_netadapter_configure_invocation(
    struct gracht_message*               message,
    const struct ctt_netadapter_session* identity,
    uint32_t                             mtu,
    uint32_t                             filter)
{
    VirtioNetDevice_t* device;
    oserr_t            status;

    VirtioNetLock();
    device = VirtioNetFindSession(message, identity);
    status = OS_EOK;

    if (device == NULL) {
        status = OS_ENOENT;
    } else if (!VirtioNetIsIdle(device)) {
        status = OS_EBUSY;
    } else if (mtu != VIRTIO_NET_MTU ||
               filter != (CTT_NETADAPTER_RX_FILTER_UNICAST | CTT_NETADAPTER_RX_FILTER_BROADCAST)) {
        status = OS_ENOTSUPPORTED;
    } else {
        device->Session.Configured = true;
    }
    ctt_netadapter_configure_response(message, status);
    VirtioNetUnlock();
}

void
ctt_netadapter_get_link_invocation(
    struct gracht_message*               message,
    const struct ctt_netadapter_session* identity)
{
    struct ctt_netadapter_link empty = { 0 };
    VirtioNetDevice_t*         device;
    oserr_t                    status;

    VirtioNetLock();
    device = VirtioNetFindSession(message, identity);
    
    // Re-read on query as well as IRQ: a lost config notification must not leave
    // the client with stale carrier state indefinitely.
    status = device ? VirtioNetReadConfiguration(device) : OS_ENOENT;
    ctt_netadapter_get_link_response(
        message,
        status,
        device ? &device->Link : &empty
    );
    VirtioNetUnlock();
}

void
ctt_netadapter_get_counters_invocation(
    struct gracht_message*               message,
    const struct ctt_netadapter_session* identity)
{
    struct ctt_netadapter_counters empty = { 0 };
    VirtioNetDevice_t*             device;

    VirtioNetLock();
    device = VirtioNetFindSession(message, identity);
    ctt_netadapter_get_counters_response(
        message,
        device ? OS_EOK : OS_ENOENT,
        device ? &device->Session.Counters : &empty
    );
    VirtioNetUnlock();
}

void
ctt_netadapter_prepare_run_invocation(
    struct gracht_message*               message,
    const struct ctt_netadapter_session* identity,
    uint64_t                             run)
{
    VirtioNetDevice_t* device;
    oserr_t            status = OS_ENOENT;

    VirtioNetLock();
    device = VirtioNetFindSession(message, identity);
    if (device) {
        VirtioNetSession_t* session = &device->Session;
        if (run && run == session->Run &&
            (session->State == VIRTIO_NET_PREPARED || session->State == VIRTIO_NET_RUNNING)) {
            status = OS_EOK;
        } else if (!run || session->Run == UINT64_MAX || run != session->Run + 1) {
            status = OS_ENOENT;
        } else if (!VirtioNetIsIdle(device) || !session->Configured || session->Faulted ||
                   session->Progress.retired_batch_id != session->Progress.consumed_batch_id) {
            status = OS_EBUSY;
        } else {
            unsigned directions = 0;
            for (uint32_t i = 0; i < session->PoolCount; ++i) {
                if (session->Pools[i].Mapped) {
                    directions |= session->Pools[i].Description.direction;
                }
            }
            status = directions == (CTT_NETADAPTER_DIRECTION_RX | CTT_NETADAPTER_DIRECTION_TX)
                             ? VirtioNetQueuesPrepare(device)
                             : OS_EINVALPARAMS;
            if (status == OS_EOK) {
                session->Run = run;
                session->State = VIRTIO_NET_PREPARED;
            } else if (device->ReceiveQueue || device->TransmitQueue) {
                VirtioNetFault(device, status);
            }
        }
    }
    ctt_netadapter_prepare_run_response(message, status);
    VirtioNetUnlock();
}

void
ctt_netadapter_start_run_invocation(
    struct gracht_message*               message,
    const struct ctt_netadapter_session* identity,
    uint64_t                             run,
    uint64_t                             fence)
{
    VirtioNetDevice_t* device;
    oserr_t            status = OS_ENOENT;

    VirtioNetLock();
    device = VirtioNetFindSession(message, identity);
    if (device && run && run == device->Session.Run) {
        VirtioNetSession_t* session = &device->Session;
        if (session->State == VIRTIO_NET_RUNNING) {
            status = fence == session->StartFence ? OS_EOK : OS_EINVALPARAMS;
        } else if (session->State != VIRTIO_NET_PREPARED || session->Faulted ||
                   fence != session->Progress.consumed_batch_id ||
                   session->Outstanding[1] < device->Info.min_rx_slots) {
            status = OS_EBUSY;
        } else {
            status = VirtioPciFinishInitialization(&device->Transport);
            if (status == OS_EOK) {
                session->StartFence = fence;
                session->State = VIRTIO_NET_RUNNING;
            } else {
                VirtioNetFault(device, status);
            }
        }
    }
    ctt_netadapter_start_run_response(message, status);
    VirtioNetUnlock();
}

void
ctt_netadapter_stop_run_invocation(
    struct gracht_message*               message,
    const struct ctt_netadapter_session* identity,
    uint64_t                             run,
    uint64_t                             fence)
{
    VirtioNetDevice_t* device;
    oserr_t            status = OS_ENOENT;

    VirtioNetLock();
    device = VirtioNetFindSession(message, identity);
    if (device != NULL) {
        status = VirtioNetStop(device, run, fence);
    }
    ctt_netadapter_stop_run_response(
        message,
        status,
        status == OS_EOK ? device->Session.StopBarrier : 0
    );
    VirtioNetUnlock();
}

void
ctt_netadapter_close_invocation(
    struct gracht_message*               message,
    const struct ctt_netadapter_session* identity)
{
    VirtioNetDevice_t* device;
    oserr_t            status = OS_ENOENT;

    VirtioNetLock();
    device = VirtioNetFindSession(message, identity);
    if (device != NULL) {
        status = VirtioNetClose(device);
    } else if (VirtioNetWasClosed(message, identity)) {
        status = OS_EOK;
    }
    ctt_netadapter_close_response(message, status);
    VirtioNetUnlock();
}

void
ctt_netadapter_post_rx_batch_invocation(
    struct gracht_message*               message,
    const struct ctt_netadapter_session* identity,
    uint64_t                             run,
    uint64_t                             batch,
    const struct ctt_netadapter_ack*     ack,
    const struct ctt_netadapter_packet*  packets,
    uint32_t                             count)
{
    VirtioNetDevice_t* device;

    VirtioNetLock();
    device = VirtioNetFindSession(message, identity);
    if (device != NULL) {
        VirtioNetSubmit(
            device,
            run,
            batch,
            ack,
            packets,
            count,
            CTT_NETADAPTER_DIRECTION_RX
        );
    }
    VirtioNetUnlock();
}

void
ctt_netadapter_submit_tx_batch_invocation(
    struct gracht_message*               message,
    const struct ctt_netadapter_session* identity,
    uint64_t                             run,
    uint64_t                             batch,
    const struct ctt_netadapter_ack*     ack,
    const struct ctt_netadapter_packet*  packets,
    uint32_t                             count)
{
    VirtioNetDevice_t* device;

    VirtioNetLock();
    device = VirtioNetFindSession(message, identity);
    if (device != NULL) {
        VirtioNetSubmit(
            device,
            run,
            batch,
            ack,
            packets,
            count,
            CTT_NETADAPTER_DIRECTION_TX
        );
    }
    VirtioNetUnlock();
}

void
ctt_netadapter_acknowledge_invocation(
    struct gracht_message*               message,
    const struct ctt_netadapter_session* identity,
    const struct ctt_netadapter_ack*     ack)
{
    VirtioNetDevice_t* device;

    VirtioNetLock();
    device = VirtioNetFindSession(message, identity);
    if (device != NULL) {
        oserr_t status = VirtioNetAcknowledge(device, ack);
        ctt_netadapter_event_ack_progress_single(
            VirtioNetServer(),
            device->Session.Owner,
            identity,
            ack,
            status,
            &device->Session.Progress
        );
    }
    VirtioNetUnlock();
}

void
ctt_netadapter_drain_invocation(
    struct gracht_message*               message,
    const struct ctt_netadapter_session* identity,
    uint64_t                             request,
    uint64_t                             after,
    uint32_t                             limit,
    const struct ctt_netadapter_ack*     ack)
{
    struct ctt_netadapter_completion records[VIRTIO_NET_BATCH];

    VirtioNetDevice_t*  device;
    VirtioNetSession_t* session;
    oserr_t             status;
    uint64_t            highest;
    uint32_t            count;

    VirtioNetLock();
    device = VirtioNetFindSession(message, identity);
    if (device == NULL || request <= device->Session.DrainId) {
        VirtioNetUnlock();
        return;
    }

    session = &device->Session;
    session->DrainId = request;

    // Poll used rings during recovery too, so a lost hardware IRQ does not leave
    // DMA-completed slots outstanding forever. Poll may emit independent pushes.
    VirtioNetPoll(device);

    status = VirtioNetAcknowledge(device, ack);
    highest = session->Progress.highest_completion_sequence;
    count = 0;
    if (status == OS_EOK) {
        if (after < session->Progress.retired_completion_sequence) {
            status = OS_ENOENT;
        } else if (after > highest || !limit || limit > VIRTIO_NET_BATCH) {
            status = OS_EINVALPARAMS;
        } else {
            count = highest - after < limit ? (uint32_t)(highest - after) : limit;
            for (uint32_t i = 0; i < count; ++i) {
                records[i] = session->Journal[(after + i) % VIRTIO_NET_JOURNAL];
            }
        }
    }

    if (count) {
        ctt_netadapter_event_completions_single(
            VirtioNetServer(),
            session->Owner,
            identity,
            request,
            records,
            count,
            &session->Progress
        );
    }

    ctt_netadapter_event_drain_end_single(
        VirtioNetServer(),
        session->Owner,
        identity,
        request,
        status,
        after,
        after + count,
        count,
        highest,
        &session->Progress
    );
    VirtioNetUnlock();
}

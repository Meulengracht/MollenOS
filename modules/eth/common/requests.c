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
#include <internal/_service.h>

#include "netadapter-private.h"

// Every handler serializes session changes with hardware polling. Reply failure
// must not undo a completed operation: clients recover with the protocol's retry
// keys or progress queries. Keep send failures at trace level since a disconnected
// client can cause repeated timeouts while the adapter remains healthy.

void
ctt_netadapter_get_info_invocation(
    _In_ struct gracht_message* message,
    _In_ uuid_t                 deviceId,
    _In_ uint32_t               port)
{
    struct ctt_netadapter_info empty = { 0 };
    EthernetDevice_t*          device;

    EthernetLock();
    device = EthernetFindDevice(deviceId);
    // The adapter has one port, numbered zero. Unknown ports return empty info.
    if (ctt_netadapter_get_info_response(
        message,
        device && !port ? OS_EOK : OS_ENOENT,
        device && !port ? &device->Common->Info : &empty
    )) {
        TRACE("ctt_netadapter_get_info_response: send failed");
    }
    EthernetUnlock();
}

void
ctt_netadapter_open_invocation(
    _In_ struct gracht_message* message,
    _In_ uuid_t                 deviceId,
    _In_ uint32_t               port,
    _In_ uint64_t               features)
{
    struct ctt_netadapter_session identity = { 0 };
    struct ctt_netadapter_link    link = { 0 };
    oserr_t                       status = OS_EOK;
    EthernetDevice_t*             device;

    EthernetLock();
    device = EthernetFindDevice(deviceId);
    if (device == NULL || port) {
        status = OS_ENOENT;
    } else if (!__IsAuthenticatedCaller(&message->security)) {
        status = OS_EPERMISSIONS;
    } else {
        status = EthernetOpen(device, message, features);
    }

    if (status == OS_EOK) {
        identity = device->Common->Session.Identity;
        link = device->Common->Link;
    }

    // The negotiated feature mask is zero because optional features are absent.
    if (ctt_netadapter_open_response(message, status, &identity, 0, &link)) {
        TRACE("ctt_netadapter_open_response: send failed");
    }
    EthernetUnlock();
}

void
ctt_netadapter_register_pool_invocation(
    _In_ struct gracht_message*               message,
    _In_ const struct ctt_netadapter_session* identity,
    _In_ uint64_t                             key,
    _In_ const struct ctt_netadapter_pool*    pool)
{
    EthernetDevice_t* device;
    uint32_t          id = 0;
    oserr_t           status = OS_ENOENT;

    EthernetLock();
    device = EthernetFindSession(message, identity);
    if (device != NULL) {
        status = EthernetPoolRegister(device, key, pool, &id);
    }
    if (ctt_netadapter_register_pool_response(message, status, id)) {
        TRACE("ctt_netadapter_register_pool_response: send failed");
    }
    EthernetUnlock();
}

void
ctt_netadapter_unregister_pool_invocation(
    _In_ struct gracht_message*               message,
    _In_ const struct ctt_netadapter_session* identity,
    _In_ uint32_t                             id)
{
    EthernetDevice_t* device;

    EthernetLock();
    device = EthernetFindSession(message, identity);
    if (ctt_netadapter_unregister_pool_response(
        message,
        device ? EthernetPoolUnregister(device, id) : OS_ENOENT
    )) {
        TRACE("ctt_netadapter_unregister_pool_response: send failed");
    }
    EthernetUnlock();
}

void
ctt_netadapter_configure_invocation(
    _In_ struct gracht_message*               message,
    _In_ const struct ctt_netadapter_session* identity,
    _In_ uint32_t                             mtu,
    _In_ uint32_t                             filter)
{
    EthernetDevice_t* device;
    oserr_t           status;

    EthernetLock();
    device = EthernetFindSession(message, identity);
    status = device ? EthernetConfigure(device, mtu, filter) : OS_ENOENT;
    if (ctt_netadapter_configure_response(message, status)) {
        TRACE("ctt_netadapter_configure_response: send failed");
    }
    EthernetUnlock();
}

void
ctt_netadapter_get_link_invocation(
    _In_ struct gracht_message*               message,
    _In_ const struct ctt_netadapter_session* identity)
{
    struct ctt_netadapter_link empty = { 0 };
    EthernetDevice_t*          device;
    oserr_t                    status;

    EthernetLock();
    device = EthernetFindSession(message, identity);

    // Re-read on query as well as IRQ: a lost config notification must not leave
    // the client with stale carrier state indefinitely.
    status = device ? EthernetReadConfiguration(device) : OS_ENOENT;
    if (ctt_netadapter_get_link_response(
        message,
        status,
        device ? &device->Common->Link : &empty
    )) {
        TRACE("ctt_netadapter_get_link_response: send failed");
    }
    EthernetUnlock();
}

void
ctt_netadapter_get_counters_invocation(
    _In_ struct gracht_message*               message,
    _In_ const struct ctt_netadapter_session* identity)
{
    struct ctt_netadapter_counters empty = { 0 };
    EthernetDevice_t*              device;

    EthernetLock();
    device = EthernetFindSession(message, identity);
    if (ctt_netadapter_get_counters_response(
        message,
        device ? OS_EOK : OS_ENOENT,
        device ? &device->Common->Session.Counters : &empty
    )) {
        TRACE("ctt_netadapter_get_counters_response: send failed");
    }
    EthernetUnlock();
}

void
ctt_netadapter_prepare_run_invocation(
    _In_ struct gracht_message*               message,
    _In_ const struct ctt_netadapter_session* identity,
    _In_ uint64_t                             run)
{
    EthernetDevice_t* device;
    oserr_t           status = OS_ENOENT;

    EthernetLock();
    device = EthernetFindSession(message, identity);
    if (device != NULL) {
        status = EthernetPrepare(device, run);
    }
    if (ctt_netadapter_prepare_run_response(message, status)) {
        TRACE("ctt_netadapter_prepare_run_response: send failed");
    }
    EthernetUnlock();
}

void
ctt_netadapter_start_run_invocation(
    _In_ struct gracht_message*               message,
    _In_ const struct ctt_netadapter_session* identity,
    _In_ uint64_t                             run,
    _In_ uint64_t                             fence)
{
    EthernetDevice_t* device;
    oserr_t           status = OS_ENOENT;

    EthernetLock();
    device = EthernetFindSession(message, identity);
    if (device != NULL) {
        status = EthernetStart(device, run, fence);
    }
    if (ctt_netadapter_start_run_response(message, status)) {
        TRACE("ctt_netadapter_start_run_response: send failed");
    }
    EthernetUnlock();
}

void
ctt_netadapter_stop_run_invocation(
    _In_ struct gracht_message*               message,
    _In_ const struct ctt_netadapter_session* identity,
    _In_ uint64_t                             run,
    _In_ uint64_t                             fence)
{
    EthernetDevice_t* device;
    oserr_t            status = OS_ENOENT;

    EthernetLock();
    device = EthernetFindSession(message, identity);
    if (device != NULL) {
        status = EthernetStop(device, run, fence);
    }
    if (ctt_netadapter_stop_run_response(
        message,
        status,
        status == OS_EOK ? device->Common->Session.StopBarrier : 0
    )) {
        TRACE("ctt_netadapter_stop_run_response: send failed");
    }
    EthernetUnlock();
}

void
ctt_netadapter_close_invocation(
    _In_ struct gracht_message*               message,
    _In_ const struct ctt_netadapter_session* identity)
{
    EthernetDevice_t* device;
    oserr_t            status = OS_ENOENT;

    EthernetLock();
    device = EthernetFindSession(message, identity);
    if (device != NULL) {
        status = EthernetClose(device);
    } else if (EthernetWasClosed(message, identity)) {
        // A lost close reply must not make the same close fail on retry.
        status = OS_EOK;
    }
    if (ctt_netadapter_close_response(message, status)) {
        TRACE("ctt_netadapter_close_response: send failed");
    }
    EthernetUnlock();
}

void
EthernetSubmit(
    _InOut_ EthernetDevice_t*                   device,
    _In_    uint64_t                            run,
    _In_    uint64_t                            batchId,
    _In_    const struct ctt_netadapter_ack*    ack,
    _In_    const struct ctt_netadapter_packet* packets,
    _In_    uint32_t                            count,
    _In_    enum ctt_netadapter_direction       direction)
{
    EthernetSession_t*                     session = &device->Common->Session;
    const struct ctt_netadapter_admission* admissions;
    oserr_t                                status;

    status = EthernetAdmitBatch(
        device,
        run,
        batchId,
        ack,
        packets,
        count,
        direction,
        &admissions
    );

    // Sending does not retire the saved batch. A lost event is recovered by
    // repeating the same request with the same packet descriptions.
    if (ctt_netadapter_event_batch_admitted_single(
        EthernetServer(),
        session->Owner,
        &session->Identity,
        run,
        batchId,
        status,
        admissions,
        status == OS_EOK ? count : 0,
        &session->Progress
    )) {
        TRACE("EthernetSubmit: device %u admission send failed", device->Common->DeviceId);
    }
}

void
ctt_netadapter_post_rx_batch_invocation(
    _In_ struct gracht_message*               message,
    _In_ const struct ctt_netadapter_session* identity,
    _In_ uint64_t                             run,
    _In_ uint64_t                             batch,
    _In_ const struct ctt_netadapter_ack*     ack,
    _In_ const struct ctt_netadapter_packet*  packets,
    _In_ uint32_t                             count)
{
    EthernetDevice_t* device;

    EthernetLock();
    device = EthernetFindSession(message, identity);
    if (device != NULL) {
        EthernetSubmit(
            device,
            run,
            batch,
            ack,
            packets,
            count,
            CTT_NETADAPTER_DIRECTION_RX
        );
    }
    // Batch requests have no direct response. An invalid session cannot receive
    // a directed event, so it is discarded without changing adapter state.
    EthernetUnlock();
}

void
ctt_netadapter_submit_tx_batch_invocation(
    _In_ struct gracht_message*               message,
    _In_ const struct ctt_netadapter_session* identity,
    _In_ uint64_t                             run,
    _In_ uint64_t                             batch,
    _In_ const struct ctt_netadapter_ack*     ack,
    _In_ const struct ctt_netadapter_packet*  packets,
    _In_ uint32_t                             count)
{
    EthernetDevice_t* device;

    EthernetLock();
    device = EthernetFindSession(message, identity);
    if (device != NULL) {
        EthernetSubmit(
            device,
            run,
            batch,
            ack,
            packets,
            count,
            CTT_NETADAPTER_DIRECTION_TX
        );
    }
    EthernetUnlock();
}

void
ctt_netadapter_acknowledge_invocation(
    _In_ struct gracht_message*               message,
    _In_ const struct ctt_netadapter_session* identity,
    _In_ const struct ctt_netadapter_ack*     ack)
{
    EthernetDevice_t* device;
    oserr_t status;

    EthernetLock();
    device = EthernetFindSession(message, identity);
    if (device != NULL) {
        status = EthernetAcknowledge(device, ack);
        if (ctt_netadapter_event_ack_progress_single(
            EthernetServer(),
            device->Common->Session.Owner,
            identity,
            ack,
            status,
            &device->Common->Session.Progress
        )) {
            TRACE("ctt_netadapter_event_ack_progress_single: send failed");
        }
    }
    EthernetUnlock();
}

void
ctt_netadapter_drain_invocation(
    _In_ struct gracht_message*               message,
    _In_ const struct ctt_netadapter_session* identity,
    _In_ uint64_t                             request,
    _In_ uint64_t                             after,
    _In_ uint32_t                             limit,
    _In_ const struct ctt_netadapter_ack*     ack)
{
    EthernetDevice_t*  device;
    EthernetSession_t* session;
    EthernetDrain_t    result;

    EthernetLock();
    device = EthernetFindSession(message, identity);
    if (device == NULL || !EthernetDrain(device, request, after, limit, ack, &result)) {
        EthernetUnlock();
        return;
    }
    session = &device->Common->Session;

    // Completion records and drain_end are separate events. Neither retires
    // journal entries. The client must acknowledge only records it received.
    if (result.Count) {
        if (ctt_netadapter_event_completions_single(
            EthernetServer(),
            session->Owner,
            identity,
            request,
            result.Records,
            result.Count,
            &session->Progress
        )) {
            TRACE("ctt_netadapter_event_completions_single: send failed");
        }
    }

    if (ctt_netadapter_event_drain_end_single(
        EthernetServer(),
        session->Owner,
        identity,
        request,
        result.Status,
        after,
        after + result.Count,
        result.Count,
        result.Highest,
        &session->Progress
    )) {
        TRACE("ctt_netadapter_event_drain_end_single: send failed");
    }
    EthernetUnlock();
}

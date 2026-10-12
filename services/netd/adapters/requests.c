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
 * Protocol handling for the adapter request scheduler.
 * 
 * Sends the requests chosen by NetAdapterNextRequest (scheduler.c) to the driver
 * and feeds decoded control responses back into the adapter core.
 */

#include "private.h"
#include "session.h"

// protocol handling
#include <ctt_netadapter_service_client.h>

static void
__GetMessageResult(
    _In_ struct AdapterEntry* entry)
{
    NetAdapterReply_t reply = { 0 };
    int               result = -1;
    int               ready;

    if (!entry->AwaitReply) {
        return;
    }
    
    ready = gracht_client_get_status(entry->Client, &entry->Context.base);
    if (ready == GRACHT_MESSAGE_INPROGRESS) {
        return;
    }

    if (ready == GRACHT_MESSAGE_COMPLETED) {
        switch (entry->Operation) {
            case SERVICE_CTT_NETADAPTER_GET_INFO_ID:
                result = ctt_netadapter_get_info_result(
                    entry->Client,
                    &entry->Context.base,
                    &reply.Status,
                    &reply.Info
                );
                break;
            case SERVICE_CTT_NETADAPTER_OPEN_ID:
                result = ctt_netadapter_open_result(
                    entry->Client,
                    &entry->Context.base,
                    &reply.Status,
                    &reply.Session,
                    &reply.Value,
                    &reply.Link
                );
                break;
            case SERVICE_CTT_NETADAPTER_REGISTER_POOL_ID:
                result = ctt_netadapter_register_pool_result(
                    entry->Client,
                    &entry->Context.base,
                    &reply.Status,
                    &reply.PoolId
                );
                break;
            case SERVICE_CTT_NETADAPTER_CONFIGURE_ID:
                result = ctt_netadapter_configure_result(
                    entry->Client,
                    &entry->Context.base,
                    &reply.Status
                );
                break;
            case SERVICE_CTT_NETADAPTER_PREPARE_RUN_ID:
                result = ctt_netadapter_prepare_run_result(
                    entry->Client,
                    &entry->Context.base,
                    &reply.Status
                );
                break;
            case SERVICE_CTT_NETADAPTER_START_RUN_ID:
                result = ctt_netadapter_start_run_result(
                    entry->Client,
                    &entry->Context.base,
                    &reply.Status
                );
                break;
            case SERVICE_CTT_NETADAPTER_STOP_RUN_ID:
                result = ctt_netadapter_stop_run_result(
                    entry->Client,
                    &entry->Context.base,
                    &reply.Status,
                    &reply.Value
                );
                break;
            case SERVICE_CTT_NETADAPTER_CLOSE_ID:
                result = ctt_netadapter_close_result(
                    entry->Client,
                    &entry->Context.base,
                    &reply.Status
                );
                break;
            case SERVICE_CTT_NETADAPTER_GET_LINK_ID:
                result = ctt_netadapter_get_link_result(
                    entry->Client,
                    &entry->Context.base,
                    &reply.Status,
                    &reply.Link
                );
                break;
            case SERVICE_CTT_NETADAPTER_GET_COUNTERS_ID:
                result = ctt_netadapter_get_counters_result(
                    entry->Client,
                    &entry->Context.base,
                    &reply.Status,
                    &reply.Counters
                );
                break;
            default:
                break;
        }
    }
    
    (void)gracht_client_abandon(entry->Client, &entry->Context.base);
    entry->AwaitReply = false;
    if (result) {
        NetAdapterSetProtocolError(entry->Adapter);
    } else {
        (void)HandleAdapterRequest(
            entry->Adapter,
            entry->Driver,
            entry->Serial,
            &reply,
            entry->Now
        );
    }
    if (!result && entry->Operation == SERVICE_CTT_NETADAPTER_OPEN_ID && reply.Status == OS_EOK) {
        entry->Session = reply.Session;
    }
}

static void
__HandleMessageRequest(
        struct AdapterEntry*       entry,
        const NetAdapterRequest_t* request)
{
    // A Gracht message ID is 32-bit. Never reuse one on an endpoint while a
    // delayed response might still exist. Keep the last IDs free so close and
    // recovery retries can still be sent.
    if (entry->SentFrames >= UINT32_MAX - 64 &&
        request->Operation != SERVICE_CTT_NETADAPTER_CLOSE_ID) {
        NetAdapterClose(entry->Adapter);
        return;
    }
    if (entry->SentFrames == UINT32_MAX - 1) {
        NetAdapterSetProtocolError(entry->Adapter);
        return;
    }
    entry->SentFrames++;
    struct vali_link_message       context = VALI_MSG_INIT_HANDLE(entry->Driver);
    struct gracht_message_context* message = &context.base;
    bool                           control = NetAdapterRequestIsControl(request);
    if (control) {
        if (entry->AwaitReply) {
            (void)gracht_client_abandon(entry->Client, &entry->Context.base);
        }
        entry->Context = context;
        message = &entry->Context.base;
        entry->Serial = request->Serial;
        entry->Operation = request->Operation;
        entry->AwaitReply = false;
    }
    int result = -1;
    switch (request->Operation) {
        case SERVICE_CTT_NETADAPTER_GET_INFO_ID:
            result =
                    ctt_netadapter_get_info(entry->Client, message, request->Device, request->Port);
            break;
        case SERVICE_CTT_NETADAPTER_OPEN_ID:
            result = ctt_netadapter_open(entry->Client,
                                         message,
                                         request->Device,
                                         request->Port,
                                         0);
            break;
        case SERVICE_CTT_NETADAPTER_REGISTER_POOL_ID:
            result = ctt_netadapter_register_pool(
                    entry->Client, message, &request->Session, request->Value, &request->Pool);
            break;
        case SERVICE_CTT_NETADAPTER_CONFIGURE_ID:
            result = ctt_netadapter_configure(entry->Client,
                                              message,
                                              &request->Session,
                                              request->Mtu,
                                              CTT_NETADAPTER_RX_FILTER_UNICAST |
                                                      CTT_NETADAPTER_RX_FILTER_BROADCAST);
            break;
        case SERVICE_CTT_NETADAPTER_PREPARE_RUN_ID:
            result = ctt_netadapter_prepare_run(
                    entry->Client, message, &request->Session, request->Run);
            break;
        case SERVICE_CTT_NETADAPTER_START_RUN_ID:
            result = ctt_netadapter_start_run(
                    entry->Client, message, &request->Session, request->Run, request->Value);
            break;
        case SERVICE_CTT_NETADAPTER_STOP_RUN_ID:
            result = ctt_netadapter_stop_run(
                    entry->Client, message, &request->Session, request->Run, request->Value);
            break;
        case SERVICE_CTT_NETADAPTER_CLOSE_ID:
            result = ctt_netadapter_close(entry->Client, message, &request->Session);
            break;
        case SERVICE_CTT_NETADAPTER_GET_LINK_ID:
            result = ctt_netadapter_get_link(entry->Client, message, &request->Session);
            break;
        case SERVICE_CTT_NETADAPTER_GET_COUNTERS_ID:
            result = ctt_netadapter_get_counters(entry->Client, message, &request->Session);
            break;
        case SERVICE_CTT_NETADAPTER_POST_RX_BATCH_ID:
            result = ctt_netadapter_post_rx_batch(entry->Client,
                                                  message,
                                                  &request->Session,
                                                  request->Run,
                                                  request->Value,
                                                  &request->Ack,
                                                  request->Packets,
                                                  request->Count);
            break;
        case SERVICE_CTT_NETADAPTER_SUBMIT_TX_BATCH_ID:
            result = ctt_netadapter_submit_tx_batch(entry->Client,
                                                    message,
                                                    &request->Session,
                                                    request->Run,
                                                    request->Value,
                                                    &request->Ack,
                                                    request->Packets,
                                                    request->Count);
            break;
        case SERVICE_CTT_NETADAPTER_ACKNOWLEDGE_ID:
            result = ctt_netadapter_acknowledge(
                    entry->Client, message, &request->Session, &request->Ack);
            break;
        case SERVICE_CTT_NETADAPTER_DRAIN_ID:
            result = ctt_netadapter_drain(entry->Client,
                                          message,
                                          &request->Session,
                                          request->Value,
                                          request->After,
                                          request->Count,
                                          &request->Ack);
            break;
    }
    if (control) {
        entry->AwaitReply = result == 0;
    }
    // If sending failed, we do not know whether the driver got the request. The retry
    // deadlines resend it with the same batch, run or registration ID, so the driver
    // can recognize the repeat. The new Gracht message ID used for each send only
    // routes the reply and has no other meaning.
}

bool
NetAdapterTransportPoll(
    _In_ struct AdapterEntry* entry,
    _In_ uint64_t             now)
{
    bool busy = false;
    
    entry->Now = now;
    
    for (int i = 0; i < NET_ADAPTER_WORK_BUDGET; ++i) {
        int result = gracht_client_wait_message(entry->Client, NULL, 0);
        if (result && errno != EPROTO && errno != ENOENT && errno != EALREADY) {
            // Vali's connectionless iprecv reports an empty nonblocking inbox
            // as ENODATA. It is not an end-of-stream or proof of driver death.
            if (errno == EPIPE) {
                NetAdapterClose(entry->Adapter);
            }
            break;
        }
        // Invalid or late frames are ignored. If a reply was lost, the request is
        // retried a limited number of times; we never pretend a packet was rejected.
        if (i == NET_ADAPTER_WORK_BUDGET - 1) {
            busy = true;
        }
    }
    
    __GetMessageResult(entry);
    for (int i = 0; i < NET_ADAPTER_WORK_BUDGET; ++i) {
        const NetAdapterRequest_t* request;
        if (NetAdapterNextRequest(entry->Adapter, now, &request) != OS_EOK) {
            break;
        }
        __HandleMessageRequest(entry, request);
        if (i == NET_ADAPTER_WORK_BUDGET - 1) {
            busy = true;
        }
    }
    return busy;
}

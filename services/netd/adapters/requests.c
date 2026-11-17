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
 * Gracht IPC transport loop for one adapter port.
 * 
 * Polls incoming Gracht messages, decodes replies for pending control requests,
 * and sends the next requests returned by NetAdapterNextRequest (scheduler.c).
 */

#include "private.h"
#include "session.h"

// Generated Gracht client stubs.
#include <ctt_netadapter_service_client.h>

// Poll for a reply to the currently pending control request, decode it, and
// forward it to HandleAdapterRequest.
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
    
    // Free Gracht's tracking state for this message before handing the reply
    // to the adapter core.
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
    // Cache the opened session ID + generation so client.c can validate
    // incoming unsolicited events before calling into the adapter core.
    if (!result && entry->Operation == SERVICE_CTT_NETADAPTER_OPEN_ID && reply.Status == OS_EOK) {
        entry->Session = reply.Session;
    }
}

// Send one scheduled request over Gracht. Control requests save entry->Context
// so __GetMessageResult can later read the reply.
static void
__HandleMessageRequest(
        struct AdapterEntry*       entry,
        const NetAdapterRequest_t* request)
{
    // Gracht message IDs are 32-bit and must not wrap while older replies might
    // still arrive. Reserve the last 64 IDs for CLOSE and retry traffic.
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
        // Drop any stale Gracht context before sending a retry or new control request.
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
    // If sending failed, the message might still have reached the driver. The
    // scheduler's retry timers resend it later with the same batch, run, or
    // registration ID so the driver can spot duplicates. The fresh Gracht
    // message ID only routes the reply.
}

bool
NetAdapterTransportPoll(
    _In_ struct AdapterEntry* entry,
    _In_ uint64_t             now)
{
    bool busy = false;
    
    entry->Now = now;
    
    // Step 1: Drain incoming Gracht messages (replies and driver events) up to
    // NET_ADAPTER_WORK_BUDGET so one noisy port cannot starve others.
    for (int i = 0; i < NET_ADAPTER_WORK_BUDGET; ++i) {
        int result = gracht_client_wait_message(entry->Client, NULL, 0);
        if (result && errno != EPROTO && errno != ENOENT && errno != EALREADY) {
            // Vali's connectionless iprecv returns ENODATA when the inbox is empty.
            // Only EPIPE signals a broken connection that warrants closing.
            if (errno == EPIPE) {
                NetAdapterClose(entry->Adapter);
            }
            break;
        }
        // Bad or late frames are ignored. Lost replies time out and retry up
        // to RetryLimit instead of pretending packets were rejected.
        if (i == NET_ADAPTER_WORK_BUDGET - 1) {
            busy = true;
        }
    }
    
    // Step 2: Check whether a pending control request has finished and advance
    // the adapter state machine if so.
    __GetMessageResult(entry);

    // Step 3: Send outgoing requests produced by the scheduler, bounded by the
    // same per-poll budget.
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

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
 * Protocol request scheduling, retry deadlines, and completion-credit recovery.
 * 
 * Request management, retry deadlines and credit recovery.
 */

#include "private.h"
#include "session.h"

// protocol handling
#include <ctt_netadapter_service_client.h>

static bool
__IsControlRequest(
    _In_ const NetAdapterRequest_t* request)
{
    if (request == NULL) {
        return false;
    }

    return request->Operation != SERVICE_CTT_NETADAPTER_POST_RX_BATCH_ID &&
           request->Operation != SERVICE_CTT_NETADAPTER_SUBMIT_TX_BATCH_ID &&
           request->Operation != SERVICE_CTT_NETADAPTER_ACKNOWLEDGE_ID &&
           request->Operation != SERVICE_CTT_NETADAPTER_DRAIN_ID;
}

static oserr_t
__InitializeRequest(
    _In_    NetworkAdapter_t*    adapter,
    _InOut_ NetAdapterRequest_t* request)
{
    // Sanitize the serial to use for this request
    if (adapter->NextSerial == UINT64_MAX) {
        return OS_EOVERFLOW;
    }

    request->Serial = adapter->NextSerial++;
    request->Device = adapter->Device;
    request->Driver = adapter->Driver;
    request->Port = adapter->Port;
    request->Mtu = adapter->Mtu;
    request->Session = adapter->Session;
    request->Run = adapter->Run;
    return OS_EOK;
}

static void
__FillACK(
    _In_    NetworkAdapter_t*          adapter,
    _InOut_ struct ctt_netadapter_ack* ack)
{
    NetBufferStats_t stats = { 0 };
    
    if (adapter->Buffers) {
        NetBuffersGetStats(adapter->Buffers, &stats);
    }

    ack->through_batch_id = adapter->Admitted;
    ack->through_completion_sequence = stats.ProcessedCompletion;
}

static void
__UpdateSentACK(
        NetworkAdapter_t*    adapter,
        NetAdapterRequest_t* request)
{
    // Update our ACK structure
    __FillACK(adapter, &request->Ack);

    // published/uncertain, never itself a retirement grant// published/uncertain, 
    // never itself a retirement grant
    adapter->SentAck = request->Ack;
}

static oserr_t
__BeginControlRequest(
    _In_ NetworkAdapter_t* adapter,
    _In_ uint8_t           operation,
    _In_ uint64_t          value)
{
    oserr_t oserr;

    // Fill in the operation and value
    memset(&adapter->Request, 0, sizeof(NetAdapterRequest_t));
    adapter->Request.Operation = operation;
    adapter->Request.Value = value;
    
    oserr = __InitializeRequest(adapter, &adapter->Request);
    if (oserr != OS_EOK) {
        return oserr;
    }

    adapter->Pending = true;
    adapter->Attempts = 0;
    adapter->Deadline = 0;
    return OS_EOK;
}

static struct AdapterBatch*
__AllocateBatch(NetworkAdapter_t* adapter)
{
    for (uint32_t i = 0; i < adapter->Window; ++i) {
        if (!adapter->Batches[i].Used) {
            struct AdapterBatch* batch = &adapter->Batches[i];
            memset(batch, 0, sizeof(struct AdapterBatch));
            return batch;
        }
    }
    return NULL;
}

static oserr_t
__InitializeBatchRequest(
    _In_  NetworkAdapter_t*           adapter,
    _In_  bool                        rx,
    _In_  uint64_t                    now,
    _Out_ const NetAdapterRequest_t** out)
{
    struct AdapterBatch* batch = NULL;
    oserr_t              oserr;

    // If this is an RX request and the retry time has 
    // not yet been reached, bail early.
    if (rx && now < adapter->RxRetryAt) {
        return OS_ENOENT;
    }
    
    batch = __AllocateBatch(adapter);
    if (!batch) {
        return OS_ENOENT;
    }
    
    oserr = NetAdapterBuildBatch(adapter, rx, &batch->Request);
    if (oserr != OS_EOK) {
        return oserr;
    }
    
    oserr = __InitializeRequest(adapter, &batch->Request);
    if (oserr != OS_EOK) {
        // prepared leases remain uncertain until close
        return oserr;
    }
    
    // Nothing that can go wrong atp, increase
    // the batch counter.
    adapter->NextBatch++;
    
    batch->Used = true;
    batch->Attempts = 1;
    batch->Deadline = __NetAdapterDeadline(now, adapter->Config.RetryMilliseconds);
    __UpdateSentACK(adapter, &batch->Request);
    
    *out = &batch->Request;
    return OS_EOK;
}

/** 
 * @brief Replay the oldest overdue unresolved batch before admitting new work.
 */
static oserr_t
__RetryBatchRequest(
    _In_  NetworkAdapter_t*           adapter,
    _In_  uint64_t                    now,
    _Out_ const NetAdapterRequest_t** out)
{
    // Retry the earliest due unresolved ID first. Never create a new batch
    // when a gap/busy/lost admission leaves an earlier outcome unknown.
    struct AdapterBatch* retry = NULL;
    for (uint32_t i = 0; i < adapter->Window; ++i) {
        struct AdapterBatch* batch = &adapter->Batches[i];
        if (batch->Used && !batch->Admitted && now >= batch->Deadline &&
            (!retry || batch->Request.Value < retry->Request.Value)) {
            retry = batch;
        }
    }

    if (retry == NULL) {
        return OS_ENOENT;
    }
    
    // Have we exhausted our attempts? then we timeout
    if (retry->Attempts >= adapter->Config.RetryLimit) {
        NetAdapterMarkFailed(adapter, OS_ETIMEOUT);
        return OS_ETIMEOUT;
    }

    retry->Attempts++;
    retry->Deadline = __NetAdapterDeadline(now, adapter->Config.RetryMilliseconds);
    __UpdateSentACK(adapter, &retry->Request);
    
    *out = &retry->Request;
    return OS_EOK;
}

/** 
 * @brief Retry unconfirmed credits independently of data and optional control replies.
 */
static oserr_t
__PrepareACK(
    _In_    NetworkAdapter_t*           adapter,
    _In_    uint64_t                    now,
    _Out_   const NetAdapterRequest_t** out,
    _InOut_ const NetBufferStats_t*     stats)
{
    bool    unconfirmed = false;
    oserr_t oserr;
    
    if (adapter->Admitted > adapter->Retired) {
        unconfirmed = true;
    } else if (stats->ProcessedCompletion > stats->AcknowledgedCompletion) {
        unconfirmed = true;
    }

    if (!unconfirmed || now < adapter->AckDeadline) {
        return OS_ENOENT;
    }
    
    // Have we exhausted our attempts? then we timeout
    if (adapter->AckAttempts >= adapter->Config.RetryLimit) {
        NetAdapterMarkFailed(adapter, OS_ETIMEOUT);
        return OS_ETIMEOUT;
    }
    
    memset(&adapter->Output, 0, sizeof(adapter->Output));
    adapter->Output.Operation = SERVICE_CTT_NETADAPTER_ACKNOWLEDGE_ID;
    
    oserr = __InitializeRequest(adapter, &adapter->Output);
    if (oserr != OS_EOK) {
        NetAdapterMarkFailed(adapter, oserr);
        return oserr;
    }

    __UpdateSentACK(adapter, &adapter->Output);
    adapter->AckAttempts++;
    adapter->AckDeadline = __NetAdapterDeadline(now, adapter->Config.RetryMilliseconds);
    
    *out = &adapter->Output;
    return OS_EOK;
}

static bool
__HasDrainCompleted(
    _In_ NetworkAdapter_t*       adapter,
    _In_ const NetBufferStats_t* stats)
{
    return adapter->Draining && adapter->DrainEnded &&
           stats->ProcessedCompletion >= adapter->DrainThrough;
}

static bool
__DrainInProgress(
    _In_ NetworkAdapter_t* adapter,
    _In_ uint64_t          now)
{
    return adapter->Draining && now >= adapter->DrainDeadline;
}

static bool
__CanStartDrain(
    _In_ NetworkAdapter_t*       adapter,
    _In_  uint64_t               now,
    _In_ const NetBufferStats_t* stats)
{
    return (!adapter->Draining && (adapter->DrainNeeded ||
           ((stats->TxOutstanding || stats->RxOutstanding) && now >= adapter->NextPoll)));
}

/** 
 * @brief Recover a bounded completion snapshot. A retry uses a fresh drain ID, while
 * the processed cursor ensures already delivered packets are not delivered again.
 */
static oserr_t
__PrepareDrainRequest(
    _In_  NetworkAdapter_t*           adapter,
    _In_  uint64_t                    now,
    _Out_ const NetAdapterRequest_t** out,
    _In_  const NetBufferStats_t*     stats)
{
    oserr_t oserr;

    // Clear the drain status if it has completed
    if (__HasDrainCompleted(adapter, stats)) {
        adapter->Draining = false;
        adapter->DrainAttempts = 0;
        adapter->NextPoll = __NetAdapterDeadline(now, adapter->Config.PollMilliseconds);
    }

    if (adapter->Buffers == NULL) {
        return OS_ENOENT;
    }

    if (__DrainInProgress(adapter, now) || __CanStartDrain(adapter, now, stats)) {
        // Have we exceeded the retry limit or reached the maximum drain ID?
        if (adapter->DrainAttempts >= adapter->Config.RetryLimit ||
            adapter->DrainId == UINT64_MAX) {
            NetAdapterMarkFailed(adapter, OS_ETIMEOUT);
            return OS_ETIMEOUT;
        }
        
        memset(&adapter->Output, 0, sizeof(NetAdapterRequest_t));
        adapter->Output.Operation = SERVICE_CTT_NETADAPTER_DRAIN_ID;
        adapter->Output.Value = ++adapter->DrainId;
        adapter->Output.After = adapter->DrainAfter = stats->ProcessedCompletion;
        adapter->Output.Count = adapter->BatchSize;
        
        oserr = __InitializeRequest(adapter, &adapter->Output);
        if (oserr != OS_EOK) {
            NetAdapterMarkFailed(adapter, oserr);
            return oserr;
        }
        
        __UpdateSentACK(adapter, &adapter->Output);
        adapter->Draining = true;
        adapter->DrainEnded = adapter->DrainNeeded = false;
        adapter->DrainSeenThrough = adapter->DrainAfter;
        adapter->DrainAttempts++;
        adapter->DrainDeadline = __NetAdapterDeadline(now, adapter->Config.RetryMilliseconds);

        *out = &adapter->Output;
        return OS_EOK;
    }
    return OS_ENOENT;
}

/**
 * @brief General request initiation for the network adapter.
 * It uses the adapter state to determine the next request to be executed, if
 * any needs to be run.
 */
static oserr_t
__BeginRequest(
    _In_  NetworkAdapter_t*           adapter,
    _In_  uint64_t                    now,
    _Out_ const NetAdapterRequest_t** out,
    _In_  const NetBufferStats_t*     stats)
{
    bool    resolved = adapter->Admitted == adapter->NextBatch - 1;
    oserr_t status = OS_ENOENT;
    
    switch (adapter->State) {
        case NET_ADAPTER_INFO:
            status = __BeginControlRequest(
                adapter,
                SERVICE_CTT_NETADAPTER_GET_INFO_ID,
                0
            );
            break;
        case NET_ADAPTER_OPEN:
            status = __BeginControlRequest(
                adapter,
                SERVICE_CTT_NETADAPTER_OPEN_ID,
                0
            );
            break;
        case NET_ADAPTER_REGISTER_TX:
        case NET_ADAPTER_REGISTER_RX:
            status = __BeginControlRequest(
                adapter,
                SERVICE_CTT_NETADAPTER_REGISTER_POOL_ID,
                0
            );
            
            if (status == OS_EOK) {
                status = NetBuffersBeginRegistration(
                    adapter->Buffers,
                    adapter->State == NET_ADAPTER_REGISTER_TX
                        ? CTT_NETADAPTER_DIRECTION_TX
                        : CTT_NETADAPTER_DIRECTION_RX,
                    &adapter->Request.Value,
                    &adapter->Request.Pool
                );
            }
            break;
        case NET_ADAPTER_CONFIGURE:
            status = __BeginControlRequest(
                adapter,
                SERVICE_CTT_NETADAPTER_CONFIGURE_ID,
                0
            );
            break;
        case NET_ADAPTER_PREPARE:
            status = __BeginControlRequest(
                adapter,
                SERVICE_CTT_NETADAPTER_PREPARE_RUN_ID,
                0
            );
            break;
        case NET_ADAPTER_PRIME:
            status = __InitializeBatchRequest(adapter, true, now, out);
            if (status == OS_ENOENT && resolved &&
                stats->RxOutstanding >= adapter->Info.min_rx_slots) {
                adapter->State = NET_ADAPTER_START;
                status = __BeginControlRequest(
                        adapter,
                        SERVICE_CTT_NETADAPTER_START_RUN_ID,
                        adapter->Admitted
                );
            }
            break;
        case NET_ADAPTER_STOPPING:
            if (!adapter->StopReplied && resolved) {
                status = __BeginControlRequest(
                    adapter,
                    SERVICE_CTT_NETADAPTER_STOP_RUN_ID,
                    adapter->Admitted
                );
            } else if (adapter->StopReplied && stats->ProcessedCompletion >= adapter->StopBarrier &&
                       stats->AcknowledgedCompletion >= adapter->StopBarrier &&
                       adapter->Retired == adapter->Admitted) {
                if (stats->TxOutstanding || stats->RxOutstanding) {
                    status = OS_EPROTOCOL;
                } else {
                    adapter->State = NET_ADAPTER_STOPPED;
                    adapter->Draining = adapter->DrainNeeded = false;
                }
            }
            break;
        case NET_ADAPTER_RUNNING:
        case NET_ADAPTER_STOPPED:
            // Stop fences packet ownership, not read-only session queries.
            // Operators need final counters/link state before restart/close.
            if (adapter->CountersNeeded) {
                status = __BeginControlRequest(
                    adapter,
                    SERVICE_CTT_NETADAPTER_GET_COUNTERS_ID,
                    0
                );
            } else if (adapter->LinkNeeded) {
                status = __BeginControlRequest(
                    adapter,
                    SERVICE_CTT_NETADAPTER_GET_LINK_ID,
                    0
                );
            }
            break;
        default:
            break;
    }
    return status;
}

static bool
__IsAdapterDead(
    _In_ NetworkAdapter_t* adapter)
{
    // Adapter is considered dead if it is quarantined, closed, or failed.
    return adapter->State == NET_ADAPTER_QUARANTINED || 
           adapter->State == NET_ADAPTER_CLOSED ||
           adapter->State == NET_ADAPTER_FAILED;
}

static bool
__IsAdapterStoppable(
    _In_ NetworkAdapter_t* adapter)
{
    return adapter->State == NET_ADAPTER_RUNNING || adapter->State == NET_ADAPTER_PRIME;
}

/** 
 * @brief Executes on one of the following requests:
 *  - lifecycle recovery
 *  - overdue batches
 *  - credit ACKs
 *  - journal recovery
 *  - lifecycle controls
 *  - new packet work
 * 
 * This priority order keeps recovery independent of optional link/counter responses.
 * @return OS_EOK if a request was successfully prepared, 
 *         OS_ENOENT if no request is pending, 
 *         or an appropriate error code otherwise.
 */
static oserr_t
__ExecuteNextRequest(
    _In_  NetworkAdapter_t*           adapter,
    _In_  uint64_t                    now,
    _Out_ const NetAdapterRequest_t** out)
{
    NetBufferStats_t stats = { 0 };
    oserr_t          status;

    // The scheduler always expects both the adapter and the
    // output pointer to be valid, otherwise it cannot safely 
    // stash a request or update caller-visible state.
    if (!adapter || !out) {
        return OS_EINVALPARAMS;
    }
    *out = NULL;

    // Once an adapter has been quarantined, closed, or failed, we stop issuing
    // new protocol work. The runtime will recover the session from the outside,
    // but no in-band scheduling decisions should try to continue driving a dead
    // adapter state machine.
    if (__IsAdapterDead(adapter)) {
        return OS_ENOENT;
    }

    // If a close has been requested and we have an active session we need
    // to check whether we must abandon the current request
    if (adapter->CloseRequested && adapter->Session.id) {
        // If a client requested shutdown while another request is still in flight, 
        // we must intentionally abandon the current pending operation so the close 
        // becomes the next message.
        if (!adapter->Pending || adapter->Request.Operation != SERVICE_CTT_NETADAPTER_CLOSE_ID) {
            adapter->Pending = false;
            adapter->State = NET_ADAPTER_CLOSING;
        }
    }

    // Retries are deadline based. If a request has stayed unresolved past its
    // retry window, we either resend the same request or declare it failed. This
    // keeps transport loss and delayed replies from deadlocking the session.
    if (adapter->Pending && now >= adapter->Deadline) {
        // Open/close requests have special semantics: a timeout here means the
        // adapter can no longer guarantee a trustworthy lifecycle transition, so
        // we quarantine it instead of silently continuing with an unverified
        // session state.
        if (adapter->Attempts >= adapter->Config.RetryLimit) {
            // The retry limit has been reached, if it's an open/close then 
            // we quarantine the adapter to prevent further unreliable operations.
            if (adapter->Request.Operation == SERVICE_CTT_NETADAPTER_OPEN_ID ||
                adapter->Request.Operation == SERVICE_CTT_NETADAPTER_CLOSE_ID) {
                adapter->LastError = OS_ETIMEOUT;
                adapter->State = NET_ADAPTER_QUARANTINED;
                return OS_ETIMEOUT;
            }

            NetAdapterMarkFailed(adapter, OS_ETIMEOUT);
        } else {
            // For requests that not lifecycle related we can try the operation again.
            adapter->Attempts++;
            adapter->Deadline = __NetAdapterDeadline(now, adapter->Config.RetryMilliseconds);
            
            *out = &adapter->Request;
            return OS_EOK;
        }
    }

    // Close is strictly higher priority than any other work. If shutdown was
    // requested, we must not keep scheduling normal data or control traffic
    // behind the close path. This also preserves the exact ordering required by
    // the adapter lifecycle, where a close must be the last authoritative
    // operation before the session is torn down.
    if (adapter->CloseRequested) {
        // If a previous operation is still unresolved, the adapter must not be
        // allowed to issue a new close request because the underlying session
        // identity is still attached to the older open or prepare state.
        if (adapter->Pending) {
            // We cannot issue a new close request while a previous one is pending
            return OS_ENOENT;
        }
        
        // When the session has already been released, there is no further control
        // flow to drive; the adapter is effectively closed and should simply
        // stop polling for more work.
        if (!adapter->Session.id) {
            adapter->State = NET_ADAPTER_CLOSED;
            return OS_ENOENT;
        }
        
        status = __BeginControlRequest(
            adapter,
            SERVICE_CTT_NETADAPTER_CLOSE_ID,
            0
        );
        if (status != OS_EOK) {
            return status;
        }
        return __ExecuteNextRequest(adapter, now, out);
    }

    // A stop request is only acted on while the run is active. We cancel queued
    // packet work first so the adapter can transition cleanly into STOPPING and
    // wait for all in-flight completion/recovery to settle before ending the run.
    if (adapter->StopRequested && __IsAdapterStoppable(adapter)) {
        NetAdapterCancelQueued(adapter);
        adapter->State = NET_ADAPTER_STOPPING;
    }

    // Batch retries have higher priority than newly created work because an
    // earlier batch may be missing the needed acknowledgements. Retrying the oldest
    // overdue batch first prevents gaps or duplicates from being mistaken for a
    // healthy stream.
    status = __RetryBatchRequest(adapter, now, out);
    if (status != OS_ENOENT) {
        return status;
    }

    // We gather the latest completion counters before deciding whether an ACK or
    // drain is needed. These counters tell us whether credits remain unconfirmed
    // or whether a drain barrier has not been fully observed.
    if (adapter->Buffers) {
        NetBuffersGetStats(adapter->Buffers, &stats);
    }

    // ACKs are independent from data work and must be sent whenever the driver
    // has unconfirmed credit state. This ensures the session can advance its
    // retirement/acknowledgement cursor without being blocked by queue pressure.
    status = __PrepareACK(adapter, now, out, &stats);
    if (status != OS_ENOENT) {
        return status;
    }

    // Drain requests are scheduled when completion progress or close/stop
    // conditions require a barrier before additional packets can be admitted.
    // The drain keeps completion processing bounded and recoverable.
    status = __PrepareDrainRequest(adapter, now, out, &stats);
    if (status != OS_ENOENT) {
        return status;
    }

    // Only if there is no pending request do we begin a new lifecycle or control
    // request. This avoids interleaving a fresh control message with an already
    // active retry or close sequence.
    if (!adapter->Pending) {
        status = __BeginRequest(adapter, now, out, &stats);
        if (status != OS_EOK && status != OS_ENOENT && status != OS_EBUSY) {
            NetAdapterMarkFailed(adapter, status);
            return status;
        }

        // If a new lifecycle phase or control request was created, return it
        // immediately; otherwise, we may still need to recurse once the request
        // is marked pending by the helper.
        if (*out) {
            return OS_EOK;
        }

        if (adapter->Pending) {
            return __ExecuteNextRequest(adapter, now, out);
        }
    }

    // The running state is the only place where in-band packet batches are
    // admitted. We try the preferred direction first, then the opposite
    // direction if the first attempt was not available, to keep RX/TX progress
    // balanced without starving either direction.
    if (adapter->State == NET_ADAPTER_RUNNING) {
        status = __InitializeBatchRequest(adapter, adapter->PreferRx, now, out);
        if (status == OS_ENOENT) {
            status = __InitializeBatchRequest(adapter, !adapter->PreferRx, now, out);
        }
        if (status == OS_EOK) {
            // Switch the transmission/reception based on the last
            // operation
            adapter->PreferRx = 
                (*out)->Operation != SERVICE_CTT_NETADAPTER_POST_RX_BATCH_ID;
        } else if (status != OS_ENOENT && status != OS_EBUSY) {
            NetAdapterMarkFailed(adapter, status);
        }
        return status == OS_EBUSY ? OS_ENOENT : status;
    }
    return OS_ENOENT;
}

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
    // delayed response might still exist. Leave room for close/recovery retries.
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
    bool                           control = __IsControlRequest(request);
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
    // Failed send remains uncertain. Core deadlines retry original batch/run/
    // registration identity; a fresh Gracht message ID is only transport routing.
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
        // Invalid/late frames are ignored by the runtime; bounded retries recover
        // missing authoritative responses, never fabricate a packet rejection.
        if (i == NET_ADAPTER_WORK_BUDGET - 1) {
            busy = true;
        }
    }
    
    __GetMessageResult(entry);
    for (int i = 0; i < NET_ADAPTER_WORK_BUDGET; ++i) {
        const NetAdapterRequest_t* request;
        if (__ExecuteNextRequest(entry->Adapter, now, &request) != OS_EOK) {
            break;
        }
        __HandleMessageRequest(entry, request);
        if (i == NET_ADAPTER_WORK_BUDGET - 1) {
            busy = true;
        }
    }
    return busy;
}

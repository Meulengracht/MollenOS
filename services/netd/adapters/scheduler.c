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
 * Protocol request scheduling and retry timers.
 *
 * Picks the next request to send to the driver, resends timed-out requests or
 * batches, and sends ACKs until the driver confirms it retired completed work.
 * Has no IPC dependencies because requests.c sends whatever NetAdapterNextRequest
 * returns.
 */

#include "private.h"
#include "session.h"

bool
NetAdapterRequestIsControl(
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
    if (adapter->Control.NextSerial == UINT64_MAX) {
        return OS_EOVERFLOW;
    }

    request->Serial = adapter->Control.NextSerial++;
    request->Device = adapter->Device;
    request->Driver = adapter->Driver;
    request->Port = adapter->Port;
    request->Mtu = adapter->Mtu;
    request->Session = adapter->Session;
    request->Run = adapter->Window.Run;
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

    ack->through_batch_id = adapter->Window.Admitted;
    ack->through_completion_sequence = stats.ProcessedCompletion;
}

static void
__UpdateSentACK(
        NetworkAdapter_t*    adapter,
        NetAdapterRequest_t* request)
{
    // Fill the newest gap-free batch ID and completion sequence to acknowledge.
    __FillACK(adapter, &request->Ack);

    // Save what we sent. Space is only freed when the driver's progress report
    // confirms retirement, never just from sending this ACK.
    adapter->Ack.Sent = request->Ack;
}

static oserr_t
__BeginControlRequest(
    _In_ NetworkAdapter_t* adapter,
    _In_ uint8_t           operation,
    _In_ uint64_t          value)
{
    oserr_t oserr;

    // Fill in the operation and value
    memset(&adapter->Control.Request, 0, sizeof(NetAdapterRequest_t));
    adapter->Control.Request.Operation = operation;
    adapter->Control.Request.Value = value;
    
    oserr = __InitializeRequest(adapter, &adapter->Control.Request);
    if (oserr != OS_EOK) {
        return oserr;
    }

    adapter->Control.Pending = true;
    adapter->Control.Attempts = 0;
    adapter->Control.Deadline = 0;
    return OS_EOK;
}

static struct AdapterBatch*
__AllocateBatch(NetworkAdapter_t* adapter)
{
    for (uint32_t i = 0; i < adapter->Window.Size; ++i) {
        if (!adapter->Window.Batches[i].Used) {
            struct AdapterBatch* batch = &adapter->Window.Batches[i];
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
    if (rx && now < adapter->Rx.RetryAt) {
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
        // Packets are already marked PENDING in the buffer manager and stay
        // reserved until the session closes.
        return oserr;
    }
    
    // Everything succeeded, so consume NextBatch and arm the retry timer.
    adapter->Window.NextBatch++;
    
    batch->Used = true;
    batch->Attempts = 1;
    batch->Deadline = __NetAdapterDeadline(now, adapter->Config.RetryMilliseconds);
    __UpdateSentACK(adapter, &batch->Request);
    
    *out = &batch->Request;
    return OS_EOK;
}

/** 
 * @brief Resend the oldest unanswered batch whose retry deadline passed.
 * Checked before building new batches so lost answers settle first.
 */
static oserr_t
__RetryBatchRequest(
    _In_  NetworkAdapter_t*           adapter,
    _In_  uint64_t                    now,
    _Out_ const NetAdapterRequest_t** out)
{
    struct AdapterBatch* retry = NULL;
    for (uint32_t i = 0; i < adapter->Window.Size; ++i) {
        struct AdapterBatch* batch = &adapter->Window.Batches[i];
        if (batch->Used && !batch->Admitted && now >= batch->Deadline &&
            (!retry || batch->Request.Value < retry->Request.Value)) {
            retry = batch;
        }
    }

    if (retry == NULL) {
        return OS_ENOENT;
    }
    
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
 * @brief Send a standalone ACK when the driver has not yet confirmed retiring
 * work we finished processing. Uses its own retry timer so it never stalls
 * waiting for data traffic.
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
    
    if (adapter->Window.Admitted > adapter->Window.Retired) {
        unconfirmed = true;
    } else if (stats->ProcessedCompletion > stats->AcknowledgedCompletion) {
        unconfirmed = true;
    }

    if (!unconfirmed || now < adapter->Ack.Deadline) {
        return OS_ENOENT;
    }
    
    if (adapter->Ack.Attempts >= adapter->Config.RetryLimit) {
        NetAdapterMarkFailed(adapter, OS_ETIMEOUT);
        return OS_ETIMEOUT;
    }
    
    memset(&adapter->Control.Output, 0, sizeof(adapter->Control.Output));
    adapter->Control.Output.Operation = SERVICE_CTT_NETADAPTER_ACKNOWLEDGE_ID;
    
    oserr = __InitializeRequest(adapter, &adapter->Control.Output);
    if (oserr != OS_EOK) {
        NetAdapterMarkFailed(adapter, oserr);
        return oserr;
    }

    __UpdateSentACK(adapter, &adapter->Control.Output);
    adapter->Ack.Attempts++;
    adapter->Ack.Deadline = __NetAdapterDeadline(now, adapter->Config.RetryMilliseconds);
    
    *out = &adapter->Control.Output;
    return OS_EOK;
}

static bool
__HasDrainCompleted(
    _In_ NetworkAdapter_t*       adapter,
    _In_ const NetBufferStats_t* stats)
{
    return adapter->Drain.Active && adapter->Drain.Ended &&
           stats->ProcessedCompletion >= adapter->Drain.Through;
}

static bool
__DrainInProgress(
    _In_ NetworkAdapter_t* adapter,
    _In_ uint64_t          now)
{
    return adapter->Drain.Active && now >= adapter->Drain.Deadline;
}

static bool
__CanStartDrain(
    _In_ NetworkAdapter_t*       adapter,
    _In_  uint64_t               now,
    _In_ const NetBufferStats_t* stats)
{
    // Start a drain right away if completions appear missing (Drain.Needed), or
    // periodically poll while packets are in flight in case an event was lost.
    return (!adapter->Drain.Active && (adapter->Drain.Needed ||
           ((stats->TxOutstanding || stats->RxOutstanding) && now >= adapter->Drain.NextPoll)));
}

/** 
 * @brief Ask the driver to resend up to BatchSize completion records after
 * ProcessedCompletion. Each attempt uses a fresh drain ID so late replies from
 * an earlier timed-out drain are ignored.
 */
static oserr_t
__PrepareDrainRequest(
    _In_  NetworkAdapter_t*           adapter,
    _In_  uint64_t                    now,
    _Out_ const NetAdapterRequest_t** out,
    _In_  const NetBufferStats_t*     stats)
{
    oserr_t oserr;

    if (__HasDrainCompleted(adapter, stats)) {
        adapter->Drain.Active = false;
        adapter->Drain.Attempts = 0;
        adapter->Drain.NextPoll = __NetAdapterDeadline(now, adapter->Config.PollMilliseconds);
    }

    if (adapter->Buffers == NULL) {
        return OS_ENOENT;
    }

    if (__DrainInProgress(adapter, now) || __CanStartDrain(adapter, now, stats)) {
        if (adapter->Drain.Attempts >= adapter->Config.RetryLimit ||
            adapter->Drain.Id == UINT64_MAX) {
            NetAdapterMarkFailed(adapter, OS_ETIMEOUT);
            return OS_ETIMEOUT;
        }
        
        memset(&adapter->Control.Output, 0, sizeof(NetAdapterRequest_t));
        adapter->Control.Output.Operation = SERVICE_CTT_NETADAPTER_DRAIN_ID;
        adapter->Control.Output.Value = ++adapter->Drain.Id;
        adapter->Control.Output.After = adapter->Drain.After = stats->ProcessedCompletion;
        adapter->Control.Output.Count = adapter->Window.BatchSize;
        
        oserr = __InitializeRequest(adapter, &adapter->Control.Output);
        if (oserr != OS_EOK) {
            NetAdapterMarkFailed(adapter, oserr);
            return oserr;
        }
        
        __UpdateSentACK(adapter, &adapter->Control.Output);
        adapter->Drain.Active = true;
        adapter->Drain.Ended = adapter->Drain.Needed = false;
        adapter->Drain.SeenThrough = adapter->Drain.After;
        adapter->Drain.Attempts++;
        adapter->Drain.Deadline = __NetAdapterDeadline(now, adapter->Config.RetryMilliseconds);

        *out = &adapter->Control.Output;
        return OS_EOK;
    }
    return OS_ENOENT;
}

/**
 * @brief Start the next lifecycle step or read-only query for the current state.
 */
static oserr_t
__BeginRequest(
    _In_  NetworkAdapter_t*           adapter,
    _In_  uint64_t                    now,
    _Out_ const NetAdapterRequest_t** out,
    _In_  const NetBufferStats_t*     stats)
{
    // True when every batch handed out so far has been admitted by the driver.
    bool    resolved = adapter->Window.Admitted == adapter->Window.NextBatch - 1;
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
                    &adapter->Control.Request.Value,
                    &adapter->Control.Request.Pool
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
            // Post initial RX buffers before starting traffic. Once all posted
            // batches are admitted and min_rx_slots are with the driver, start.
            status = __InitializeBatchRequest(adapter, true, now, out);
            if (status == OS_ENOENT && resolved &&
                stats->RxOutstanding >= adapter->Info.min_rx_slots) {
                NetAdapterSetState(adapter, NET_ADAPTER_START);
                status = __BeginControlRequest(
                        adapter,
                        SERVICE_CTT_NETADAPTER_START_RUN_ID,
                        adapter->Window.Admitted
                );
            }
            break;
        case NET_ADAPTER_STOPPING:
            // Wait for every sent batch to be answered before sending STOP_RUN.
            // Afterward, wait until all completions up to StopBarrier are
            // processed, acknowledged, and retired before moving to STOPPED.
            if (!adapter->Intent.StopReplied && resolved) {
                status = __BeginControlRequest(
                    adapter,
                    SERVICE_CTT_NETADAPTER_STOP_RUN_ID,
                    adapter->Window.Admitted
                );
            } else if (adapter->Intent.StopReplied && stats->ProcessedCompletion >= adapter->Intent.StopBarrier &&
                       stats->AcknowledgedCompletion >= adapter->Intent.StopBarrier &&
                       adapter->Window.Retired == adapter->Window.Admitted) {
                if (stats->TxOutstanding || stats->RxOutstanding) {
                    status = OS_EPROTOCOL;
                } else {
                    NetAdapterSetState(adapter, NET_ADAPTER_STOPPED);
                    adapter->Drain.Active = adapter->Drain.Needed = false;
                }
            }
            break;
        case NET_ADAPTER_RUNNING:
        case NET_ADAPTER_STOPPED:
            // Read-only link and counter queries stay allowed while stopped so
            // consumers can read final stats before restarting or closing.
            if (adapter->Intent.CountersNeeded) {
                status = __BeginControlRequest(
                    adapter,
                    SERVICE_CTT_NETADAPTER_GET_COUNTERS_ID,
                    0
                );
            } else if (adapter->Intent.LinkNeeded) {
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
 * @brief Pick the next request to send, ordered by priority:
 *  1. Control retries, timeouts, and close requests.
 *  2. Overdue TX/RX batches waiting for admission.
 *  3. Standalone ACKs for unconfirmed retired work.
 *  4. Completion drains when completions are missing or polls are due.
 *  5. New lifecycle steps or link/counter queries.
 *  6. New RX and TX packet batches while running.
 */
oserr_t
NetAdapterNextRequest(
    _In_  NetworkAdapter_t*           adapter,
    _In_  uint64_t                    now,
    _Out_ const NetAdapterRequest_t** out)
{
    NetBufferStats_t stats = { 0 };
    oserr_t          status;

    if (!adapter || !out) {
        return OS_EINVALPARAMS;
    }
    *out = NULL;

    // Quarantined, closed, and failed adapters emit no new requests until
    // recovered externally via NetAdapterRetry.
    if (__IsAdapterDead(adapter)) {
        return OS_ENOENT;
    }

    // If a close was requested while another control request (other than CLOSE)
    // is pending, drop the old request so CLOSE goes out next.
    if (adapter->Intent.CloseRequested && adapter->Session.id) {
        if (!adapter->Control.Pending || adapter->Control.Request.Operation != SERVICE_CTT_NETADAPTER_CLOSE_ID) {
            adapter->Control.Pending = false;
            NetAdapterSetState(adapter, NET_ADAPTER_CLOSING);
        }
    }

    // Check whether the active control request timed out.
    if (adapter->Control.Pending && now >= adapter->Control.Deadline) {
        if (adapter->Control.Attempts >= adapter->Config.RetryLimit) {
            // Quarantine on OPEN or CLOSE timeouts because we cannot tell if
            // the driver created or closed the session.
            if (adapter->Control.Request.Operation == SERVICE_CTT_NETADAPTER_OPEN_ID ||
                adapter->Control.Request.Operation == SERVICE_CTT_NETADAPTER_CLOSE_ID) {
                adapter->LastError = OS_ETIMEOUT;
                NetAdapterSetState(adapter, NET_ADAPTER_QUARANTINED);
                return OS_ETIMEOUT;
            }

            NetAdapterMarkFailed(adapter, OS_ETIMEOUT);
        } else {
            adapter->Control.Attempts++;
            adapter->Control.Deadline = __NetAdapterDeadline(now, adapter->Config.RetryMilliseconds);
            
            *out = &adapter->Control.Request;
            return OS_EOK;
        }
    }

    // Close takes priority over all other traffic so shutdown happens promptly
    // and CLOSE is the final control message sent on the session.
    if (adapter->Intent.CloseRequested) {
        // Wait for any in-flight CLOSE or OPEN request to finish first.
        if (adapter->Control.Pending) {
            return OS_ENOENT;
        }
        
        // No active driver session remains, so move straight to CLOSED.
        if (!adapter->Session.id) {
            NetAdapterSetState(adapter, NET_ADAPTER_CLOSED);
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
        return NetAdapterNextRequest(adapter, now, out);
    }

    // Cancel locally queued TX frames before moving into STOPPING to drain
    // frames already handed to the driver.
    if (adapter->Intent.StopRequested && __IsAdapterStoppable(adapter)) {
        NetAdapterCancelQueued(adapter);
        NetAdapterSetState(adapter, NET_ADAPTER_STOPPING);
    }

    // Resend overdue unanswered batches before creating new batches so the
    // driver's admission sequence keeps moving forward.
    status = __RetryBatchRequest(adapter, now, out);
    if (status != OS_ENOENT) {
        return status;
    }

    if (adapter->Buffers) {
        NetBuffersGetStats(adapter->Buffers, &stats);
    }

    // Send standalone ACKs even when packet queues are idle or full so the
    // driver can free retired batch and completion records.
    status = __PrepareACK(adapter, now, out, &stats);
    if (status != OS_ENOENT) {
        return status;
    }

    // Fetch missing completions or run the periodic completion poll.
    status = __PrepareDrainRequest(adapter, now, out, &stats);
    if (status != OS_ENOENT) {
        return status;
    }

    // Start at most one control request at a time.
    if (!adapter->Control.Pending) {
        status = __BeginRequest(adapter, now, out, &stats);
        if (status != OS_EOK && status != OS_ENOENT && status != OS_EBUSY) {
            NetAdapterMarkFailed(adapter, status);
            return status;
        }

        if (*out) {
            return OS_EOK;
        }

        // __BeginRequest armed Control.Request with Deadline == 0. Recurse once
        // so the timeout check above sends the first attempt right away.
        if (adapter->Control.Pending) {
            return NetAdapterNextRequest(adapter, now, out);
        }
    }

    // While running, alternate between RX and TX batches so neither direction
    // starves the other.
    if (adapter->State == NET_ADAPTER_RUNNING) {
        status = __InitializeBatchRequest(adapter, adapter->Window.PreferRx, now, out);
        if (status == OS_ENOENT) {
            status = __InitializeBatchRequest(adapter, !adapter->Window.PreferRx, now, out);
        }
        if (status == OS_EOK) {
            adapter->Window.PreferRx = 
                (*out)->Operation != SERVICE_CTT_NETADAPTER_POST_RX_BATCH_ID;
        } else if (status != OS_ENOENT && status != OS_EBUSY) {
            NetAdapterMarkFailed(adapter, status);
        }
        return status == OS_EBUSY ? OS_ENOENT : status;
    }
    return OS_ENOENT;
}

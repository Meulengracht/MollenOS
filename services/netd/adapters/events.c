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
 * Driver event checking and handling.
 * 
 * Checks admission, completion, drain, and ACK events from the driver and
 * updates the session state. Has no IPC dependencies because client.c copies
 * generated callback data into a NetAdapterEvent_t first.
 */

#include "private.h"
#include "session.h"

static bool
__PacketIdsEqual(
        const struct ctt_netadapter_packet_id* left,
        const struct ctt_netadapter_packet_id* right)
{
    return left->queue_id == right->queue_id && 
           left->pool_id == right->pool_id &&
           left->slot_id == right->slot_id &&
           left->submission_sequence == right->submission_sequence;
}

/**
 * @brief Check that the driver's progress report agrees with itself and with
 * what netd has sent so far.
 * @param adapter Adapter to check against.
 * @param progress Progress snapshot attached to an incoming event.
 * @return true if the progress numbers are consistent, false otherwise.
 */
static bool
__IsSessionProgressValid(
        NetworkAdapter_t*                     adapter,
        const struct ctt_netadapter_progress* progress)
{
    // Every event carries a snapshot of the driver's session progress. Because
    // events can arrive out of order, a report may look older than one we saw
    // earlier. Here we only check self-consistency against what netd has sent,
    // while __HandleSessionProgress makes sure local progress only moves forward.
    //
    // Rules checked:
    // - Retired work cannot exceed consumed batches or highest completions.
    // - Consumed batches must be strictly below NextBatch (never sent yet).
    // - Retired batches and completions cannot go past the latest ACK we sent,
    //   since only our ACK lets the driver drop saved copies of old work.
    return progress->retired_batch_id <= progress->consumed_batch_id &&
           progress->consumed_batch_id < adapter->Window.NextBatch &&
           progress->retired_completion_sequence <= progress->highest_completion_sequence &&
           progress->retired_batch_id <= adapter->Ack.Sent.through_batch_id &&
           progress->retired_completion_sequence <= adapter->Ack.Sent.through_completion_sequence;
}

/** 
 * @brief Merge a validated progress report into the adapter state.
 * Only ever moves local counters forward, never back. When the driver reports
 * that it retired acknowledged batches or completions, those window slots are
 * freed for reuse. Sending an ACK alone never frees space.
 *
 * @param adapter Adapter to update.
 * @param progress Validated progress snapshot.
 * @return OS_EOK on success, or an error if confirming completions failed.
 */
static oserr_t
__HandleSessionProgress(
    _In_ NetworkAdapter_t*                     adapter,
    _In_ const struct ctt_netadapter_progress* progress)
{
    NetBufferStats_t stats;
    NetBuffersGetStats(adapter->Buffers, &stats);
    
    // Reset the ACK retry timer only when the driver confirms newer work than
    // before. Old or repeated reports must not keep pushing the timeout back.
    if (progress->retired_batch_id > adapter->Window.Retired ||
        progress->retired_completion_sequence > stats.AcknowledgedCompletion) {
        adapter->Ack.Attempts = 0;
        adapter->Ack.Deadline = 0;
    }
    
    // Free saved batch copies up to retired_batch_id so those window slots can
    // hold new batches. Window.Retired only moves forward.
    if (progress->retired_batch_id > adapter->Window.Retired) {
        for (uint32_t i = 0; i < adapter->Window.Size; ++i) {
            if (adapter->Window.Batches[i].Used &&
                adapter->Window.Batches[i].Request.Value <= progress->retired_batch_id) {
                adapter->Window.Batches[i].Used = false;
            }
        }
        adapter->Window.Retired = progress->retired_batch_id;
    }
    
    // Track the newest completion sequence produced by the driver.
    if (progress->highest_completion_sequence > adapter->Window.Highest) {
        adapter->Window.Highest = progress->highest_completion_sequence;
    }
    
    // Request a drain if the driver is ahead of what we have processed and no
    // drain is currently running.
    if (!adapter->Drain.Active && adapter->Window.Highest > stats.ProcessedCompletion) {
        adapter->Drain.Needed = true;
    }
    
    // Tell the buffer manager which completions the driver has confirmed retired.
    return NetBuffersConfirmAcknowledged(
        adapter->Buffers,
        &adapter->Session,
        progress->retired_completion_sequence
    );
}

/** 
 * @brief Apply a batch admission event telling netd which packets the driver
 * accepted or rejected.
 *
 * Every entry in the event is checked before modifying any leases. Late
 * duplicate admissions (for example after resending a batch) must match the
 * results saved the first time and never fire completion callbacks again.
 *
 * @param adapter Adapter to update.
 * @param event Admission event from the driver.
 * @param now Current monotonic time in milliseconds.
 * @return OS_EOK on success, OS_ENOENT if stale, or an error code on protocol failure.
 */
static oserr_t
__HandleAdmission(
    _In_ NetworkAdapter_t*        adapter,
    _In_ const NetAdapterEvent_t* event,
    _In_ uint64_t                 now)
{
    struct AdapterBatch* batch;
    struct AdapterBatch* next;
    uint32_t             accepted = 0;

    // Ignore admissions from earlier runs or batches already retired.
    if (event->Run != adapter->Window.Run || event->Id <= adapter->Window.Retired) {
        return OS_ENOENT;
    }
    
    batch = NetAdapterFindBatch(adapter, event->Id);
    if (!batch) {
        return OS_EPROTOCOL;
    }
    
    // Failed admission replies carry no per-packet statuses. Ignore them and
    // let the normal retry timer handle resending the batch up to RetryLimit.
    if (event->Status != OS_EOK) {
        if (event->Count) {
            return OS_EPROTOCOL;
        }
        return OS_EOK;
    }
    
    // A successful reply must cover every packet in the batch and show that the
    // driver has consumed at least this batch ID.
    if (event->Count != batch->Request.Count || event->Progress.consumed_batch_id < event->Id) {
        return OS_EPROTOCOL;
    }
    
    // Check every admission record before modifying any leases or callbacks.
    for (uint32_t i = 0; i < event->Count; ++i) {
        if (!__PacketIdsEqual(&event->Admissions[i].id, &batch->Request.Packets[i].id)) {
            return OS_EPROTOCOL;
        }

        if (!OSERR_VALID(event->Admissions[i].status) ||
            (batch->Admitted && batch->Results[i] != event->Admissions[i].status)) {
            return OS_EPROTOCOL;
        }
        
        if (!batch->Admitted) {
            oserr_t oserr = NetBuffersValidateAdmission(
                adapter->Buffers,
                &adapter->Session,
                &event->Admissions[i]
            );
            if (oserr != OS_EOK) {
                return oserr;
            }
        }
    }
    // Duplicate admission for an already-admitted batch: nothing more to apply.
    if (batch->Admitted) {
        return OS_EOK;
    }
    
    // Apply each packet's admission result. Rejected packets release right away.
    // Accepted packets stay with the driver, unless their completion beat the
    // admission reply (ready == true), in which case we release them now.
    for (uint32_t i = 0; i < event->Count; ++i) {
        NetBufferLease_t lease;
        bool             ready;
        oserr_t          status;
        
        status = NetBuffersAdmission(
            adapter->Buffers,
            &adapter->Session,
            &event->Admissions[i],
            &lease,
            &ready
        );
        if (status != OS_EOK) {
            return status;
        }
        
        batch->Results[i] = event->Admissions[i].status;
        if (batch->Results[i] != OS_EOK) {
            struct AdapterLease* entry = NetAdapterFindLease(adapter, &lease);
            if (entry == NULL) {
                return OS_EPROTOCOL;
            }
            
            status = NetAdapterReleaseLease(
                adapter,
                entry,
                batch->Results[i],
                false
            );
        } else {
            accepted++;
            
            if (ready) {
                status = NetAdapterReleaseCompletedLease(adapter, &lease);
            }
        }
        if (status != OS_EOK) {
            return status;
        }
    }
    batch->Admitted = true;
    
    // Advance Window.Admitted across contiguous admitted batches so the next ACK
    // can let the driver drop them. Stop at the first batch still waiting.
    while ((next = NetAdapterFindBatch(adapter, adapter->Window.Admitted + 1)) && next->Admitted) {
        adapter->Window.Admitted++;
    }
    
    // On RX, fail the adapter if RetryLimit batches in a row have zero accepted
    // buffers. Wait RetryMilliseconds before refilling after a partial reject.
    if (batch->Request.Operation == SERVICE_CTT_NETADAPTER_POST_RX_BATCH_ID) {
        if (accepted) {
            adapter->Rx.Failures = 0;
        } else if (++adapter->Rx.Failures >= adapter->Config.RetryLimit) {
            return OS_EBUFFER;
        }
        
        if (accepted == event->Count) {
            adapter->Rx.RetryAt = now;
        } else {
            adapter->Rx.RetryAt = __NetAdapterDeadline(
                now,
                adapter->Config.RetryMilliseconds
            );
        }
    }
    return OS_EOK;
}

/** 
 * @brief Validate and apply a chunk of completion records from the driver.
 * Every record in the chunk is validated before any packet is delivered or
 * freed. Duplicate packet IDs inside one chunk are rejected.
 *
 * @param adapter Adapter receiving the completions.
 * @param event Completion event.
 * @return OS_EOK on success, OS_ENOENT for stale drain chunks, OS_EBUSY if
 *         earlier completions are missing, or OS_EPROTOCOL on invalid records.
 */
static oserr_t
__HandleCompletions(
    _In_ NetworkAdapter_t*        adapter,
    _In_ const NetAdapterEvent_t* event)
{
    oserr_t oserr = OS_EOK;
    
    // Reject empty completion events. A nonzero ID ties the chunk to an active
    // drain request, so late chunks from timed-out drains are ignored.
    if (!event->Count) {
        return OS_EPROTOCOL;
    }
    if (event->Id && (!adapter->Drain.Active || event->Id != adapter->Drain.Id)) {
        return OS_ENOENT;
    }
    
    // Validate the entire chunk before releasing any buffers or firing callbacks.
    for (uint32_t i = 0; i < event->Count; ++i) {
        const struct ctt_netadapter_completion* completion = &event->Completions[i];
        oserr_t                                 valid;

        // Sequence 0 is unused, and records cannot exceed the driver's newest.
        if (!completion->completion_sequence ||
            completion->completion_sequence > event->Progress.highest_completion_sequence) {
            oserr = OS_EPROTOCOL;
            break;
        }
        
        // Records inside a single chunk must have consecutive sequence numbers.
        if (i && (event->Completions[i - 1].completion_sequence == UINT64_MAX ||
                  completion->completion_sequence !=
                          event->Completions[i - 1].completion_sequence + 1)) {
            oserr = OS_EPROTOCOL;
            break;
        }

        // Drain chunks must stay inside the range requested for this drain.
        if (event->Id &&
            (completion->completion_sequence <= adapter->Drain.After ||
             completion->completion_sequence - adapter->Drain.After > adapter->Window.BatchSize ||
             (adapter->Drain.Ended && completion->completion_sequence > adapter->Drain.Through))) {
            oserr = OS_EPROTOCOL;
            break;
        }
        
        // Reject two completion records for the same packet within one chunk.
        for (uint32_t j = 0; j < i; ++j) {
            if (__PacketIdsEqual(&completion->id, &event->Completions[j].id)) {
                oserr = OS_EPROTOCOL;
            }
        }
        
        valid = NetBuffersValidateCompletion(
            adapter->Buffers,
            &adapter->Session,
            completion
        );

        // OS_EEXISTS means an exact duplicate of an already-stored completion.
        if (valid != OS_EOK && valid != OS_EEXISTS) {
            oserr = valid;
        }

        if (oserr != OS_EOK) {
            break;
        }
    }
    // All records passed validation. Apply them and deliver completed packets.
    if (oserr == OS_EOK) {
        for (uint32_t i = 0; i < event->Count && oserr == OS_EOK; ++i) {
            oserr = NetAdapterCompletePacket(adapter, &event->Completions[i]);
        }
    }
    
    // Track the highest sequence received in this drain. The scheduler waits
    // until all sequences up to Drain.Through are processed before finishing.
    if (oserr == OS_EOK && event->Id &&
        event->Completions[event->Count - 1].completion_sequence > adapter->Drain.SeenThrough) {
        adapter->Drain.SeenThrough = event->Completions[event->Count - 1].completion_sequence;
    }
    
    // OS_EBUSY means a gap exceeded the completion window. Trigger a drain to
    // fetch the missing completions instead of failing the adapter.
    if (oserr == OS_EBUSY) {
        adapter->Drain.Needed = true;
        return oserr;
    }
    return oserr;
}

/** 
 * @brief Process the end marker for a drain request.
 * The marker reports the sequence range (After + 1 .. Through) and the driver's
 * newest sequence at the time (Highest). Chunks may still arrive after this
 * marker, so the scheduler finishes the drain only once local processing catches
 * up to Through.
 *
 * @param adapter Adapter handling the drain.
 * @param event Drain-end event from the driver.
 * @return OS_EOK if valid, OS_ENOENT if from an older drain, or OS_EPROTOCOL.
 */
static oserr_t
__HandleDrainEnd(
    _In_ NetworkAdapter_t*        adapter,
    _In_ const NetAdapterEvent_t* event)
{
    if (!adapter->Drain.Active || event->Id != adapter->Drain.Id) {
        return OS_ENOENT;
    }
    
    if (event->After != adapter->Drain.After) {
        return OS_EPROTOCOL;
    }
    
    if (event->After > UINT64_MAX - event->Count) {
        return OS_EPROTOCOL;
    }
    
    if (event->Through != event->After + event->Count) {
        return OS_EPROTOCOL;
    }
    
    if (event->Highest > event->Progress.highest_completion_sequence) {
        return OS_EPROTOCOL;
    }

    if (event->Status != OS_EOK && event->Count) {
        return OS_EPROTOCOL;
    }
    if (event->Status == OS_EOK) {
        if (event->Through > event->Highest || event->Through < adapter->Drain.SeenThrough) {
            return OS_EPROTOCOL;
        }
        // Zero records is only valid when the driver had nothing newer than After.
        if (!event->Count && event->Highest != event->After) {
            return OS_EPROTOCOL;
        }
        // Repeated end markers must match the original Through and Highest.
        if (adapter->Drain.Ended && (event->Through != adapter->Drain.Through ||
                                     event->Highest != adapter->Drain.Highest)) {
            return OS_EPROTOCOL;
        }
    }
    
    if (event->Status == OS_EOK) {
        adapter->Drain.Through = event->Through;
        adapter->Drain.Highest = event->Highest;
        adapter->Drain.Ended = true;
        adapter->Intent.LinkNeeded = true;
    }
    
    // Failed drains stay active and retry with a new drain ID once their
    // deadline expires.
    return OS_EOK;
}

/** 
 * @brief Validate an ACK confirmation event from the driver.
 * Checks that the echoed ACK values stay within what netd sent, and on success
 * confirms the driver's retired progress covers the echoed range.
 *
 * @param adapter Adapter receiving the confirmation.
 * @param event ACK progress event.
 * @return OS_EOK or driver failure status, or OS_EPROTOCOL on invalid data.
 */
static oserr_t
__ValidateACK(
    _In_ NetworkAdapter_t*        adapter,
    _In_ const NetAdapterEvent_t* event)
{
    if (event->Count) {
        return OS_EPROTOCOL;
    }
    
    if (event->Ack.through_batch_id > adapter->Ack.Sent.through_batch_id) {
        return OS_EPROTOCOL;
    }
    if (event->Ack.through_completion_sequence > adapter->Ack.Sent.through_completion_sequence) {
        return OS_EPROTOCOL;
    }
    
    // On success, the driver's retired counts must reach the echoed ACK.
    if (event->Status == OS_EOK &&
        event->Progress.retired_batch_id < event->Ack.through_batch_id) {
        return OS_EPROTOCOL;
    }
   
    if (event->Status == OS_EOK &&
        event->Progress.retired_completion_sequence < event->Ack.through_completion_sequence) {
        return OS_EPROTOCOL;
    }
    return event->Status;
}

static oserr_t
__ValidateEvent(
        NetworkAdapter_t*        adapter,
        uuid_t                   driver,
        const NetAdapterEvent_t* event)
{
    // Ignore stale or late events once closing starts or while quarantined.
    if (!NetAdapterMatchesSession(adapter, driver, &event->Session) || !adapter->Buffers ||
        adapter->Intent.CloseRequested || adapter->State == NET_ADAPTER_CLOSED ||
        adapter->State == NET_ADAPTER_QUARANTINED) {
        return OS_ENOENT;
    }
    
    // Invalid counts, status codes, or inconsistent progress fail the adapter.
    if (event->Count > adapter->Window.BatchSize || event->Status < OS_EOK ||
        event->Status >= __OS_ECOUNT || !__IsSessionProgressValid(adapter, &event->Progress)) {
        NetAdapterMarkFailed(adapter, OS_EPROTOCOL);
        return OS_EPROTOCOL;
    }
    return OS_EOK;
}

oserr_t
NetAdapterEvent(
    _In_ NetworkAdapter_t*        adapter,
    _In_ uuid_t                   driver,
    _In_ const NetAdapterEvent_t* event,
    _In_ uint64_t                 now)
{
    oserr_t oserr;

    if (!adapter || !event) {
        return OS_EINVALPARAMS;
    }

    oserr = __ValidateEvent(adapter, driver, event);
    if (oserr != OS_EOK) {
        return oserr;
    }

    switch (event->Operation) {
        case SERVICE_CTT_NETADAPTER_EVENT_BATCH_ADMITTED_ID:
            oserr = __HandleAdmission(adapter, event, now);
            break;
        case SERVICE_CTT_NETADAPTER_EVENT_COMPLETIONS_ID:
            oserr = __HandleCompletions(adapter, event);
            // When completions are missing, __HandleCompletions sets Drain.Needed
            // and returns OS_EBUSY. Return OS_EOK without applying this event's
            // progress report, newer events will update progress later.
            if (oserr == OS_EBUSY) {
                return OS_EOK;
            }
            break;
        case SERVICE_CTT_NETADAPTER_EVENT_DRAIN_END_ID:
            oserr = __HandleDrainEnd(adapter, event);
            break;
        case SERVICE_CTT_NETADAPTER_EVENT_ACK_PROGRESS_ID:
            oserr = __ValidateACK(adapter, event);
            break;
        default:
            return OS_EINVALPARAMS;
    }

    // Stale or unrelated events change nothing and are not treated as errors.
    if (oserr == OS_ENOENT) {
        return oserr;
    }

    if (oserr == OS_EOK) {
        oserr = __HandleSessionProgress(adapter, &event->Progress);
    }
    if (oserr != OS_EOK) {
        NetAdapterMarkFailed(adapter, oserr);
    }
    return oserr;
}

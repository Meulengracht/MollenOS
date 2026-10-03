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
 * Gracht client setup and generated protocol callback dispatch.
 * 
 * Handling of the netadapter client API and session management.
 */

#include <ioset.h>
#include <errno.h>
#include <string.h>

// protocol clients
#include <ctt_netadapter_service_client.h>

#include "private.h"
#include "session.h"

int
NetAdapterClientCreate(
    _In_  int                eventSet,
    _In_  gracht_protocol_t* protocol,
    _Out_ gracht_client_t**  out)
{
    struct gracht_link_vali*      link;
    gracht_client_configuration_t config;
    gracht_client_t*              client;
    int                           iod;
    int                           status;
    
    // The reason we create a dedicated gracht link for each adapter is to
    // make sure each adapter has their own IPC buffer. If we consolidated the
    // IPC client into the one provided by the crt (GetGrachtClient()), we would
    // maybe end up congesting the IPC link under heavy load.
    status = gracht_link_vali_create(&link);
    if (status) {
        return status;
    }
    
    // Setting the timeout to 1 essentially means we never really block on
    // sending messages.
    gracht_link_vali_set_send_timeout(link, 1);

    gracht_client_configuration_init(&config);
    gracht_client_configuration_set_link(&config, (struct gracht_link*)link);

    // Set the maximum message size for the client to the frame limit, but
    // accounting for the UUID overhead.
    gracht_client_configuration_set_max_msg_size(
        &config,
        CTT_NETADAPTER_LIMIT_FRAME_BYTES + sizeof(uuid_t)
    );
    
    // From here on the client manages the link, and frees it if creation fails.
    status = gracht_client_create(&config, &client);
    if (status) {
        return status;
    }
   
    // Connect the client to initialize the IPC.
    status = gracht_client_connect(client);
    if (status) {
        gracht_client_shutdown(client);
        return status;
    }

    // Register the adapter protocol
    gracht_client_register_protocol(client, protocol);

    // The worker thread will poll this I/O descriptor for incoming events,
    // so we add it to the i/o set.
    iod = gracht_client_iod(client);
    status = ioset_ctrl(
        eventSet,
        IOSET_ADD,
        iod,
        &(struct ioset_event){
            .events = IOSETIN | IOSETLVT, 
            .data.iod = iod
        }
    );
    if (status) {
        gracht_client_shutdown(client);
        return status;
    }
    *out = client;
    return 0;
}

void
NetAdapterClientDestroy(
    _In_ int              eventSet,
    _In_ gracht_client_t* client)
{
    if (client == NULL) {
        return;
    }
    
    ioset_ctrl(eventSet, IOSET_DEL, gracht_client_iod(client), NULL);
    gracht_client_shutdown(client);
}

static struct AdapterEntry*
__FindSession(
    _In_ gracht_client_t*                     client,
    _In_ const struct ctt_netadapter_session* session)
{
    struct AdapterEntry* entry = NetAdapterRegistryFindClient(client);
    
    if (entry == NULL) {
        return NULL;
    }

    // Reject the entry if the session ID or generation does not match.
    if (entry->Session.id != session->id || entry->Session.generation != session->generation) {
        return NULL;
    }
    return entry;
}

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
 * @brief Validate the progress against the current state of the network adapter.
 *
 * @param adapter The network adapter whose state is being validated against.
 * @param progress The progress to validate.
 * @return true if the progress is valid, false otherwise.
 */
static bool
__IsSessionProgressValid(
        NetworkAdapter_t*                     adapter,
        const struct ctt_netadapter_progress* progress)
{
    // The driver attaches a progress report to every event. A report describes the
    // whole session at the time it was sent, and reports can arrive out of order, so
    // a report may be older than one we already applied. Here we only check that the
    // report agrees with itself and with what netd has actually sent;
    // __HandleSessionProgress makes sure applying it never moves our values backwards.
    //
    // The driver can only retire work (be completely finished with it) that it has
    // already taken: the retired batch ID cannot exceed the consumed batch ID, and the
    // retired completion sequence cannot exceed the highest completion sequence it
    // has produced. The consumed batch ID must be below NextBatch, the ID netd will
    // hand out next, so the driver cannot claim to have consumed a batch that was
    // never sent. Finally, the driver may only retire work covered by an ACK netd has
    // actually sent. Our ACK is what allows the driver to drop the copies it keeps
    // for resending batches and completion records; the fact that it has consumed or
    // produced the work does not give it that permission on its own.
    return progress->retired_batch_id <= progress->consumed_batch_id &&
           progress->consumed_batch_id < adapter->Window.NextBatch &&
           progress->retired_completion_sequence <= progress->highest_completion_sequence &&
           progress->retired_batch_id <= adapter->Ack.Sent.through_batch_id &&
           progress->retired_completion_sequence <= adapter->Ack.Sent.through_completion_sequence;
}

/** 
 * @brief Apply a progress report from the driver that already passed
 * __IsSessionProgressValid. Reports can arrive late or out of order, so each of our
 * values is only ever moved forward, never back to an older value.
 *
 * When the driver reports that it has retired work (it is completely finished with
 * batches and completion records we acknowledged), the space we kept for resending
 * those batches and for tracking those completions can be reused. Sending an ACK
 * ourselves, or learning that the driver has more work, does not free that space;
 * only the driver's report of retirement does.
 * @param adapter The network adapter whose state is being updated.
 * @param progress The validated progress snapshot to merge.
 * @return An error code indicating success or failure of the merge operation.
 *
 */
static oserr_t
__HandleSessionProgress(
    _In_ NetworkAdapter_t*                     adapter,
    _In_ const struct ctt_netadapter_progress* progress)
{
    NetBufferStats_t stats;
    NetBuffersGetStats(adapter->Buffers, &stats);
    
    // Only a report that retires more work than before shows that our ACKs are
    // getting through. In that case reset the ACK attempt count and deadline, so the
    // work still waiting for confirmation gets a full set of retries. Repeated or
    // older reports must not do this, or they could postpone the ACK timeout forever.
    if (progress->retired_batch_id > adapter->Window.Retired ||
        progress->retired_completion_sequence > stats.AcknowledgedCompletion) {
        adapter->Ack.Attempts = 0;
        adapter->Ack.Deadline = 0;
    }
    
    // The driver has retired every batch up to retired_batch_id, so we no longer need
    // our copies of them for resending. Free only the window slots of those batches.
    // Window.Retired only moves forward, so a delayed older report cannot free newer
    // batches or undo a retirement we already recorded.
    if (progress->retired_batch_id > adapter->Window.Retired) {
        for (uint32_t i = 0; i < adapter->Window.Size; ++i) {
            if (adapter->Window.Batches[i].Used &&
                adapter->Window.Batches[i].Request.Value <= progress->retired_batch_id) {
                adapter->Window.Batches[i].Used = false;
            }
        }
        adapter->Window.Retired = progress->retired_batch_id;
    }
    
    // highest_completion_sequence is the newest completion record the driver has
    // produced. Some records up to it may not have reached us yet, so it only tells
    // us a drain may be needed; it does not mean those completions were processed.
    if (progress->highest_completion_sequence > adapter->Window.Highest) {
        adapter->Window.Highest = progress->highest_completion_sequence;
    }
    
    // Ask for a drain when no drain is running and the driver has reported more
    // completions than we have processed. A drain that is already running is
    // allowed to finish first; newer completions are picked up by the next one.
    if (!adapter->Drain.Active && adapter->Window.Highest > stats.ProcessedCompletion) {
        adapter->Drain.Needed = true;
    }
    
    // Let the buffer manager free the completion space the driver has now retired.
    // It rejects retirement of records we have not yet processed in order without
    // gaps, and lowers its count of unacknowledged completions only once per record.
    return NetBuffersConfirmAcknowledged(
        adapter->Buffers,
        &adapter->Session,
        progress->retired_completion_sequence
    );
}

/** 
 * @brief Apply the driver's answer to a submitted RX or TX batch, which says for each
 * packet whether the driver accepted it (admitted it). The whole answer is checked
 * before any lease is changed, so a bad answer leaves nothing half-applied. The same
 * answer can arrive more than once, for example after we resent the batch; a late
 * duplicate must match the results stored the first time, and must never complete a
 * user's packet a second time.
 * @param adapter The network adapter whose state is being updated.
 * @param event The network adapter event containing the admission information.
 * @param now The current timestamp.
 * @return An error code indicating success or failure of the admission application.
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

    // An answer from a different run, or for a batch the driver already retired, is
    // outdated. It must not change the current run's leases or Window.Admitted.
    if (event->Run != adapter->Window.Run || event->Id <= adapter->Window.Retired) {
        return OS_ENOENT;
    }
    
    batch = NetAdapterFindBatch(adapter, event->Id);
    // The batch is not retired, yet we have no stored copy of it. Without that copy we
    // cannot tell which packets the answer refers to, so trusting it would be unsafe.
    if (!batch) {
        return OS_EPROTOCOL;
    }
    
    // Only a successful answer contains per-packet results. A failed answer may be a
    // late reply to an earlier send of this batch, so it must not undo a success that
    // came after it. Ignore it and let the batch's normal retry schedule, which has a
    // limited number of attempts, settle the outcome.
    if (event->Status != OS_EOK) {
        if (event->Count) {
            return OS_EPROTOCOL;
        }
        return OS_EOK;
    }
    
    // A successful answer must have a result for every packet we submitted, and the
    // driver's progress must show it has consumed at least this batch. Anything else
    // is a partial answer or belongs to another batch.
    if (event->Count != batch->Request.Count || event->Progress.consumed_batch_id < event->Id) {
        return OS_EPROTOCOL;
    }
    
    // Do an initial verification of the admissions against the request and cached results.
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
    // The batch was already admitted, so this is a repeated answer. Leases and
    // callbacks were updated the first time; applying it again would do that twice.
    if (batch->Admitted) {
        return OS_EOK;
    }
    
    // Apply each packet's result. A rejected packet's lease is released right away
    // with the rejection status. An accepted packet stays managed by the driver until
    // its completion arrives. If the completion already arrived before this answer,
    // we now know the packet was both accepted and finished, so its lease is released.
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
    
    // Move Window.Admitted past every batch that is admitted with no unanswered batch
    // before it. Only this gap-free range is acknowledged to the driver, which lets
    // it drop its stored copies of those batches. A later batch may already be
    // admitted while an earlier one still waits for its answer; that stops the advance.
    while ((next = NetAdapterFindBatch(adapter, adapter->Window.Admitted + 1)) && next->Admitted) {
        adapter->Window.Admitted++;
    }
    
    // For RX, count how many batches in a row had every buffer rejected; one accepted
    // buffer resets the count, and reaching RetryLimit fails the adapter. If any buffer
    // was rejected, wait RetryMilliseconds before posting replacements instead of
    // retrying immediately.
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
 * @brief Apply a chunk of completion records (the driver's final result for each
 * submitted packet). Every record is checked against the current buffer state before
 * any of them is applied, so a bad chunk changes nothing. Two records in one chunk
 * may not refer to the same packet, even if their sequence numbers differ.
 * @param adapter The network adapter instance.
 * @param event The completion event to validate and apply.
 * @return OS_EOK if all completions are valid, or an error code otherwise.
 */
static oserr_t
__HandleCompletions(
    _In_ NetworkAdapter_t*        adapter,
    _In_ const NetAdapterEvent_t* event)
{
    oserr_t oserr = OS_EOK;
    
    // A completion event must carry at least one record; an empty one tells us
    // nothing. A nonzero ID means the chunk answers a drain request and must belong
    // to the drain that is running now. Late chunks from an earlier drain attempt
    // that timed out are ignored, so they cannot change the current drain.
    if (!event->Count) {
        return OS_EPROTOCOL;
    }
    if (event->Id && (!adapter->Drain.Active || event->Id != adapter->Drain.Id)) {
        return OS_ENOENT;
    }
    
    // Check the whole chunk before handing any buffer back or calling callbacks.
    // Every sequence number must be nonzero, no higher than the newest sequence the
    // driver says it has produced, and exactly one above the previous record in this
    // chunk. Separate events may still arrive out of order or more than once.
    for (uint32_t i = 0; i < event->Count; ++i) {
        const struct ctt_netadapter_completion* completion = &event->Completions[i];
        oserr_t                                 valid;

        // Sequence 0 is never used, and a record cannot be newer than the highest
        // completion sequence reported in this event's progress.
        if (!completion->completion_sequence ||
            completion->completion_sequence > event->Progress.highest_completion_sequence) {
            oserr = OS_EPROTOCOL;
            break;
        }
        
        // Ensure the completion sequence is contiguous within this chunk.
        if (i && (event->Completions[i - 1].completion_sequence == UINT64_MAX ||
                  completion->completion_sequence !=
                          event->Completions[i - 1].completion_sequence + 1)) {
            oserr = OS_EPROTOCOL;
            break;
        }

        // A drain returns at most BatchSize records, starting right after
        // Drain.After. Once its end marker has arrived, no chunk may contain
        // records past the end that the marker reported.
        if (event->Id &&
            (completion->completion_sequence <= adapter->Drain.After ||
             completion->completion_sequence - adapter->Drain.After > adapter->Window.BatchSize ||
             (adapter->Drain.Ended && completion->completion_sequence > adapter->Drain.Through))) {
            oserr = OS_EPROTOCOL;
            break;
        }
        
        // Two final results in one chunk cannot be for the same packet. Each could
        // still pass the check below on its own, because that check only looks at
        // the state from before this event.
        for (uint32_t j = 0; j < i; ++j) {
            if (__PacketIdsEqual(&completion->id, &event->Completions[j].id)) {
                oserr = OS_EPROTOCOL;
            }
        }
        
        // The buffer manager checks the session, that the packet is managed by the
        // driver, that the result fields make sense, and that the record is not too
        // far ahead of what we have processed. A repeat of a record it already
        // stored is harmless (OS_EEXISTS), but a conflicting record fails.
        valid = NetBuffersValidateCompletion(
            adapter->Buffers,
            &adapter->Session,
            completion
        );

        // Valid, accepted status codes are OS_EOK and OS_EEXISTS. Any other
        // result indicates a failure that should halt further processing.
        if (valid != OS_EOK && valid != OS_EEXISTS) {
            oserr = valid;
        }

        // break the loop if an error occurred
        if (oserr != OS_EOK) {
            break;
        }
    }
    // Every record passed, so apply them now. The buffer manager advances its
    // processed count over records that follow on without gaps. Each packet's
    // completion callback runs only once; an exact repeat of a record does nothing.
    if (oserr == OS_EOK) {
        for (uint32_t i = 0; i < event->Count && oserr == OS_EOK; ++i) {
            oserr = NetAdapterCompletePacket(adapter, &event->Completions[i]);
        }
    }
    
    // Remember the highest sequence applied in this drain. This alone does not
    // finish the drain: the scheduler waits until every record up to the drain's
    // end has been processed with no gaps.
    if (oserr == OS_EOK && event->Id &&
        event->Completions[event->Count - 1].completion_sequence > adapter->Drain.SeenThrough) {
        adapter->Drain.SeenThrough = event->Completions[event->Count - 1].completion_sequence;
    }
    
    // OS_EBUSY means a record is too far ahead of what we have processed because
    // earlier records are missing. Ask for a drain to fetch them instead of failing
    // the adapter.
    if (oserr == OS_EBUSY) {
        adapter->Drain.Needed = true;
        return oserr;
    }
    return oserr;
}

/** 
 * @brief Handle the end marker of a drain. The marker says which completion records
 * the drain covers (those after After, up to and including Through) and the newest
 * record the driver had at that time (Highest). It does not mean every chunk of the
 * drain has arrived; the scheduler finishes the drain only once every record up to
 * Through has been processed locally.
 * @param adapter The network adapter handling the drain.
 * @param event The drain end event containing snapshot information.
 * @return OS_EOK if the drain end is valid and processed successfully.
 *         OS_EPROTOCOL if the event violates protocol constraints.
 *         OS_ENOENT if the event does not belong to the current drain.
 *         Other error codes as appropriate for specific failure conditions.
 */
static oserr_t
__HandleDrainEnd(
    _In_ NetworkAdapter_t*        adapter,
    _In_ const NetAdapterEvent_t* event)
{
    // An end marker belongs only to the current drain attempt. A late marker
    // from a timed-out attempt must not finish or alter its replacement.
    if (!adapter->Drain.Active || event->Id != adapter->Drain.Id) {
        return OS_ENOENT;
    }
    
    // The marker must start at the same point (After) that this drain asked for.
    if (event->After != adapter->Drain.After) {
        return OS_EPROTOCOL;
    }
    
    // Check for overflow before computing After + Count, the last record covered.
    if (event->After > UINT64_MAX - event->Count) {
        return OS_EPROTOCOL;
    }
    
    if (event->Through != event->After + event->Count) {
        return OS_EPROTOCOL;
    }
    
    // The newest record in the marker cannot be newer than the newest record in the
    // driver's own progress report.
    if (event->Highest > event->Progress.highest_completion_sequence) {
        return OS_EPROTOCOL;
    }

    // A failed drain sends no records, so its count must be zero. Its other values
    // are informational only.
    if (event->Status != OS_EOK && event->Count) {
        return OS_EPROTOCOL;
    }
    if (event->Status == OS_EOK) {
        // On success, the range must end at or before Highest, and must include
        // every record already received in this drain's completion chunks.
        if (event->Through > event->Highest || event->Through < adapter->Drain.SeenThrough) {
            return OS_EPROTOCOL;
        }
        // A drain may return no records only if the driver had nothing newer than
        // After.
        if (!event->Count && event->Highest != event->After) {
            return OS_EPROTOCOL;
        }
        // If the end marker arrives again, it must report the same Through and
        // Highest as the first time.
        if (adapter->Drain.Ended && (event->Through != adapter->Drain.Through ||
                                     event->Highest != adapter->Drain.Highest)) {
            return OS_EPROTOCOL;
        }
    }
    
    // Save where this drain ends and schedule another pass. The end marker alone
    // does not mean all chunks have arrived: the scheduler only finishes this
    // drain once every record up to Drain.Through has been processed locally.
    if (event->Status == OS_EOK) {
        adapter->Drain.Through = event->Through;
        adapter->Drain.Highest = event->Highest;
        adapter->Drain.Ended = true;
        adapter->Intent.LinkNeeded = true;
    }
    
    // A failed drain stays active with its current deadline and attempt count. Once
    // the deadline passes, the scheduler sends the drain again with a new drain ID.
    // This holds for every error, such as the driver being busy or the requested
    // records having been retired while the drain was in flight.
    return OS_EOK;
}

/** 
 * @brief Check the driver's reply to an ACK. The reply repeats (echoes) the ACK
 * values it received, and its progress report says how much work the driver has
 * retired. Both are checked against the ACK values netd actually sent.
 * @param adapter The network adapter instance.
 * @param event The ACK event to validate.
 * @return OS_EOK if the ACK is valid, or an appropriate error code otherwise.
 *
 */
static oserr_t
__ValidateACK(
    _In_ NetworkAdapter_t*        adapter,
    _In_ const NetAdapterEvent_t* event)
{
    // An ACK confirmation carries no packet records.
    if (event->Count) {
        return OS_EPROTOCOL;
    }
    
    // The echoed values cannot be ahead of the newest ACK netd has sent, even if the
    // driver reports an error. Echoes of older ACKs are fine, since each ACK also
    // covers everything before it.
    if (event->Ack.through_batch_id > adapter->Ack.Sent.through_batch_id) {
        return OS_EPROTOCOL;
    }
    if (event->Ack.through_completion_sequence > adapter->Ack.Sent.through_completion_sequence) {
        return OS_EPROTOCOL;
    }
    
    // On success the driver promises it retired every batch and completion record
    // covered by the echoed ACK, so its progress must show at least that much. The
    // progress report, not the echo, is what lets us reuse the freed space.
    if (event->Status == OS_EOK &&
        event->Progress.retired_batch_id < event->Ack.through_batch_id) {
        return OS_EPROTOCOL;
    }
   
    if (event->Status == OS_EOK &&
        event->Progress.retired_completion_sequence < event->Ack.through_completion_sequence) {
        return OS_EPROTOCOL;
    }
    // A failed ACK does not confirm the echoed values, although the progress report
    // may still show earlier retirement. Return the failure so it can be recovered.
    return event->Status;
}

static oserr_t
__ValidateEvent(
        NetworkAdapter_t*        adapter,
        uuid_t                   driver,
        const NetAdapterEvent_t* event)
{
    if (!adapter || !event) {
        return OS_EINVALPARAMS;
    }

    if (!NetAdapterMatchesSession(adapter, driver, &event->Session) || !adapter->Buffers ||
        adapter->Intent.CloseRequested || adapter->State == NET_ADAPTER_CLOSED ||
        adapter->State == NET_ADAPTER_QUARANTINED) {
        return OS_ENOENT;
    }
    
    if (event->Count > adapter->Window.BatchSize || event->Status < OS_EOK ||
        event->Status >= __OS_ECOUNT || !__IsSessionProgressValid(adapter, &event->Progress)) {
        NetAdapterMarkFailed(adapter, OS_EPROTOCOL);
        return OS_EPROTOCOL;
    }
    return OS_EOK;
}

void
ctt_netadapter_event_batch_admitted_invocation(
        gracht_client_t*                       client,
        const struct ctt_netadapter_session*   session,
        const uint64_t                         run,
        const uint64_t                         batch,
        const oserr_t                          status,
        const struct ctt_netadapter_admission* records,
        const uint32_t                         count,
        const struct ctt_netadapter_progress*  progress)
{
    struct AdapterEntry* entry = __FindSession(client, session);
    NetAdapterEvent_t    event;
    oserr_t              oserr;
    if (!entry) {
        return;
    }
    
    if (count > NET_ADAPTER_BATCH_MAX) {
        NetAdapterSetProtocolError(entry->Adapter);
        return;
    }
    
    event = (NetAdapterEvent_t){
        .Operation = SERVICE_CTT_NETADAPTER_EVENT_BATCH_ADMITTED_ID,
        .Session = *session,
        .Run = run,
        .Id = batch,
        .Status = status,
        .Count = count,
        .Progress = *progress
    };
    if (count) {
        memcpy(event.Admissions, records, count * sizeof(*records));
    }

    oserr = __ValidateEvent(entry->Adapter, entry->Adapter->Driver, &event);
    if (oserr != OS_EOK) {
        return;
    }

    oserr = __HandleAdmission(entry->Adapter, &event, entry->Now);
    switch (oserr) {
        case OS_ENOENT:
            return;
        case OS_EOK:
            oserr = __HandleSessionProgress(entry->Adapter, &event.Progress);
        default:
            break;
    }
    if (oserr != OS_EOK) {
        NetAdapterMarkFailed(entry->Adapter, oserr);
    }
}

void
ctt_netadapter_event_completions_invocation(
        gracht_client_t*                        client,
        const struct ctt_netadapter_session*    session,
        const uint64_t                          request,
        const struct ctt_netadapter_completion* records,
        const uint32_t                          count,
        const struct ctt_netadapter_progress*   progress)
{
    struct AdapterEntry* entry = __FindSession(client, session);
    NetAdapterEvent_t    event;
    oserr_t              status;
    
    if (!entry) {
        return;
    }
    
    if (count > NET_ADAPTER_BATCH_MAX) {
        NetAdapterSetProtocolError(entry->Adapter);
        return;
    }
    event = (NetAdapterEvent_t){
        .Operation = SERVICE_CTT_NETADAPTER_EVENT_COMPLETIONS_ID,
        .Session = *session,
        .Id = request,
        .Count = count,
        .Progress = *progress
    };
    if (count) {
        memcpy(event.Completions, records, count * sizeof(*records));
    }

    status = __ValidateEvent(entry->Adapter, entry->Adapter->Driver, &event);
    if (status != OS_EOK) {
        return;
    }

    status = __HandleCompletions(entry->Adapter, &event);
    // Earlier completion records are missing and a drain was requested to fetch
    // them. Do not fail the adapter, and skip this event's progress report; later
    // events carry newer reports.
    if (status == OS_EBUSY) {
        return;
    }

    switch (status) {
        case OS_ENOENT:
            return;
        case OS_EOK:
            status = __HandleSessionProgress(entry->Adapter, &event.Progress);
        default:
            break;
    }
    if (status != OS_EOK) {
        NetAdapterMarkFailed(entry->Adapter, status);
    }
}

void
ctt_netadapter_event_drain_end_invocation(
        gracht_client_t*                      client,
        const struct ctt_netadapter_session*  session,
        const uint64_t                        id,
        const oserr_t                         status,
        const uint64_t                        after,
        const uint64_t                        through,
        const uint32_t                        count,
        const uint64_t                        highest,
        const struct ctt_netadapter_progress* progress)
{
    struct AdapterEntry* entry = __FindSession(client, session);
    NetAdapterEvent_t    event;
    oserr_t              oserr;
    
    if (!entry) {
        return;
    }

    event = (NetAdapterEvent_t){
        .Operation = SERVICE_CTT_NETADAPTER_EVENT_DRAIN_END_ID,
        .Session = *session,
        .Id = id,
        .Status = status,
        .After = after,
        .Through = through,
        .Count = count,
        .Highest = highest,
        .Progress = *progress
    };

    oserr = __ValidateEvent(entry->Adapter, entry->Adapter->Driver, &event);
    if (oserr != OS_EOK) {
        return;
    }
    
    oserr = __HandleDrainEnd(entry->Adapter, &event);
    switch (oserr) {
        case OS_ENOENT:
            return;
        case OS_EOK:
            oserr = __HandleSessionProgress(entry->Adapter, &event.Progress);
        default:
            break;
    }
    if (oserr != OS_EOK) {
        NetAdapterMarkFailed(entry->Adapter, oserr);
    }
}

void
ctt_netadapter_event_ack_progress_invocation(
        gracht_client_t*                      client,
        const struct ctt_netadapter_session*  session,
        const struct ctt_netadapter_ack*      ack,
        const oserr_t                         status,
        const struct ctt_netadapter_progress* progress)
{
    struct AdapterEntry* entry = __FindSession(client, session);
    NetAdapterEvent_t    event;
    oserr_t              oserr;
    
    if (!entry) {
        return;
    }

    event = (NetAdapterEvent_t){
        .Operation = SERVICE_CTT_NETADAPTER_EVENT_ACK_PROGRESS_ID,
        .Session = *session,
        .Status = status,
        .Ack = *ack,
        .Progress = *progress
    };

    oserr = __ValidateEvent(entry->Adapter, entry->Adapter->Driver, &event);
    if (oserr != OS_EOK) {
        return;
    }
    
    oserr = __ValidateACK(entry->Adapter, &event);
    switch (oserr) {
        case OS_ENOENT:
            return;
        case OS_EOK:
            oserr = __HandleSessionProgress(entry->Adapter, &event.Progress);
        default:
            break;
    }
    if (oserr != OS_EOK) {
        NetAdapterMarkFailed(entry->Adapter, oserr);
    }
}

void
ctt_netadapter_event_link_changed_invocation(
        gracht_client_t*                     client,
        const struct ctt_netadapter_session* session,
        const struct ctt_netadapter_link*    link)
{
    struct AdapterEntry* entry = __FindSession(client, session);
    if (entry) {
        (void)NetAdapterLinkChanged(entry->Adapter, entry->Driver, session, link);
    }
}

void
ctt_netadapter_event_fault_invocation(
        gracht_client_t*                     client,
        const struct ctt_netadapter_session* session,
        const struct ctt_netadapter_fault*   fault)
{
    struct AdapterEntry* entry = __FindSession(client, session);
    (void)fault;
    
    if (entry) {
        NetAdapterClose(entry->Adapter);
    }
}

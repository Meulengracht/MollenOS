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
 * Network Manager
 * - Contains the implementation of the network-manager which keeps track
 *   of sockets, network interfaces and connectivity status
 * 
 * Handling of the netadapter client API and session management.
 */

#include <ioset.h>
#include <errno.h>
#include <string.h>

// protocol clients
#include <ctt_netadapter_service_client.h>

#include "private.h"

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
    
    // Creation takes ownership of the link, including error cleanup.
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
    // Progress is a session-wide snapshot, so its cursors need not be newer
    // than the last snapshot we received. Validate its internal ordering and
    // provenance here; ApplyProgress merges valid snapshots monotonically.
    //
    // Retired batches/completions must be prefixes of work the driver reports
    // consuming/producing. consumed_batch_id must be below NextBatch because
    // NextBatch is the next ID netd will issue, so this excludes claims about
    // batches that do not exist. Finally, retirement is allowed only through
    // ACK cursors netd has published: those ACKs authorize replay/journal
    // capacity reuse, whereas consumed/highest cursors alone do not.
    return progress->retired_batch_id <= progress->consumed_batch_id &&
           progress->consumed_batch_id < adapter->NextBatch &&
           progress->retired_completion_sequence <= progress->highest_completion_sequence &&
           progress->retired_batch_id <= adapter->SentAck.through_batch_id &&
           progress->retired_completion_sequence <= adapter->SentAck.through_completion_sequence;
}

/** 
 * @brief Merge a validated session snapshot without regressing local cursors. Driver
 * retirement confirms that ACKed replay/journal capacity can be reused; merely
 * sending an ACK or learning that work exists does not release that capacity.
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
    
    // Only new confirmed retirement is evidence that ACK delivery is making
    // progress. Give the remaining unconfirmed credits a fresh retry budget;
    // duplicate or older snapshots must not keep postponing a timeout.
    if (progress->retired_batch_id > adapter->Retired ||
        progress->retired_completion_sequence > stats.AcknowledgedCompletion) {
        adapter->AckAttempts = 0;
        adapter->AckDeadline = 0;
    }
    
    // A retired batch prefix no longer needs local replay storage. Release
    // only slots in that prefix and keep the cursor at its greatest value so
    // delayed snapshots cannot free newer batches or undo retirement.
    if (progress->retired_batch_id > adapter->Retired) {
        for (uint32_t i = 0; i < adapter->Window; ++i) {
            if (adapter->Batches[i].Used &&
                adapter->Batches[i].Request.Value <= progress->retired_batch_id) {
                adapter->Batches[i].Used = false;
            }
        }
        adapter->Retired = progress->retired_batch_id;
    }
    
    // Highest reveals journal records that may still be missing locally; it
    // is a drain hint, not proof those completions have been processed.
    if (progress->highest_completion_sequence > adapter->Highest) {
        adapter->Highest = progress->highest_completion_sequence;
    }
    
    // Request a drain only when idle and behind the reported high-water mark.
    // Finish an in-flight bounded snapshot before starting another for later pushes.
    if (!adapter->Draining && adapter->Highest > stats.ProcessedCompletion) {
        adapter->DrainNeeded = true;
    }
    
    // The buffer manager accepts only retirement within its locally processed
    // prefix and decrements unacknowledged completion credits exactly once.
    return NetBuffersConfirmAcknowledged(
        adapter->Buffers,
        &adapter->Session,
        progress->retired_completion_sequence
    );
}

/** 
 * @brief Validate the entire admission before changing leases. A late duplicate must
 * match retained results and cannot complete a user submission twice.
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

    // A different run or an already retired batch is stale; its result must
    // not affect the current run's leases or admission cursor.
    if (event->Run != adapter->Run || event->Id <= adapter->Retired) {
        return OS_ENOENT;
    }
    
    batch = NetAdapterFindBatch(adapter, event->Id);
    // A nonretired ID without retained replay state cannot be tied to a
    // submission, so treating its records as authoritative would be unsafe.
    if (!batch) {
        return OS_EPROTOCOL;
    }
    
    // Only successful admission carries per-packet results. A failure may be
    // an older response to a replay and cannot revoke a subsequent success;
    // leave unresolved batches on their existing finite retry schedule.
    if (event->Status != OS_EOK) {
        if (event->Count) {
            return OS_EPROTOCOL;
        }
        return OS_EOK;
    }
    
    // Success must describe every submitted packet and claim that the driver
    // consumed at least this batch, not an unrelated or partial result.
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
    // Replayed success is already reflected in leases and callbacks; never
    // apply it a second time.
    if (batch->Admitted) {
        return OS_EOK;
    }
    
    // Record each admission. Rejections terminate their leases immediately;
    // accepted packets stay owned by the driver unless a completion arrived
    // first, in which case both facts are known and the lease can be released.
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
    
    // Only a contiguous admitted prefix can be ACKed to retire driver replay
    // state. A later batch may be known while an earlier result is still missing.
    while ((next = NetAdapterFindBatch(adapter, adapter->Admitted + 1)) && next->Admitted) {
        adapter->Admitted++;
    }
    
    // For RX, any accepted buffers break a run of total admission failures;
    // repeated total rejection is fatal. If some buffers were rejected, pause
    // before posting replacements instead of retrying immediately.
    if (batch->Request.Operation == SERVICE_CTT_NETADAPTER_POST_RX_BATCH_ID) {
        if (accepted) {
            adapter->RxFailures = 0;
        } else if (++adapter->RxFailures >= adapter->Config.RetryLimit) {
            return OS_EBUFFER;
        }
        
        if (accepted == event->Count) {
            adapter->RxRetryAt = now;
        } else {
            adapter->RxRetryAt = NetAdapterDeadline(
                now,
                adapter->Config.RetryMilliseconds
            );
        }
    }
    return OS_EOK;
}

/** 
 * @brief Validate every completion against the same ownership state before applying
 * any record. Duplicate descriptor IDs are invalid even at distinct journal positions.
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
    
    // Completion events carry records; an empty chunk cannot establish any
    // journal progress. A nonzero ID belongs to one active drain attempt, so
    // delayed chunks from a superseded attempt cannot change its snapshot.
    if (!event->Count) {
        return OS_EPROTOCOL;
    }
    if (event->Id && (!adapter->Draining || event->Id != adapter->DrainId)) {
        return OS_ENOENT;
    }
    
    // Check the whole chunk before changing buffer ownership or delivering
    // callbacks. Sequence numbers must be real, within the driver's reported
    // high-water mark, and contiguous inside this chunk even though separate
    // events may arrive out of order or repeat.
    for (uint32_t i = 0; i < event->Count; ++i) {
        const struct ctt_netadapter_completion* completion = &event->Completions[i];
        oserr_t                                 valid;

        // Sanitize the completion sequence, it must be valid compared
        // the highest completion sequence reported in the event's progress.
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

        // A drain is bounded to BatchSize records after DrainAfter. Once its
        // end marker arrives, no chunk may extend beyond that snapshot's end.
        if (event->Id &&
            (completion->completion_sequence <= adapter->DrainAfter ||
             completion->completion_sequence - adapter->DrainAfter > adapter->BatchSize ||
             (adapter->DrainEnded && completion->completion_sequence > adapter->DrainThrough))) {
            oserr = OS_EPROTOCOL;
            break;
        }
        
        // Two terminal records cannot name the same submission, even
        // when each individually validates against the pre-event state.
        for (uint32_t j = 0; j < i; ++j) {
            if (__PacketIdsEqual(&completion->id, &event->Completions[j].id)) {
                oserr = OS_EPROTOCOL;
            }
        }
        
        // The buffer manager checks session, descriptor ownership, result
        // shape and its journal window. An identical retained record is a
        // harmless replay, but a conflicting record must fail the validation.
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
    // Apply only after every record passes validation. The buffer manager
    // advances its processed prefix over contiguous records; completion
    // callbacks happen once, and an exact replay has no second effect.
    if (oserr == OS_EOK) {
        for (uint32_t i = 0; i < event->Count && oserr == OS_EOK; ++i) {
            oserr = NetAdapterCompletePacket(adapter, &event->Completions[i]);
        }
    }
    
    // Track the largest sequence successfully applied for this drain. The
    // scheduler separately waits for the contiguous processed prefix before
    // considering the snapshot finished.
    if (oserr == OS_EOK && event->Id &&
        event->Completions[event->Count - 1].completion_sequence > adapter->DrainSeenThrough) {
        adapter->DrainSeenThrough = event->Completions[event->Count - 1].completion_sequence;
    }
    
    // A record beyond the local journal window needs its missing predecessors
    // drained first. Keep that gap recoverable instead of failing the adapter.
    if (oserr == OS_EBUSY) {
        adapter->DrainNeeded = true;
        return oserr;
    }
    return oserr;
}

/** 
 * @brief An end marker describes the drain snapshot, not delivery of all its chunks.
 * The scheduler waits until local processing reaches Through before finishing it.
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
    if (!adapter->Draining || event->Id != adapter->DrainId) {
        return OS_ENOENT;
    }
    
    // The marker must describe the prefix requested by this drain.
    if (event->After != adapter->DrainAfter) {
        return OS_EPROTOCOL;
    }
    
    // Check for overflow before calculating the end of that prefix.
    if (event->After > UINT64_MAX - event->Count) {
        return OS_EPROTOCOL;
    }
    
    if (event->Through != event->After + event->Count) {
        return OS_EPROTOCOL;
    }
    
    // The reported snapshot cannot exceed the driver's own progress.
    if (event->Highest > event->Progress.highest_completion_sequence) {
        return OS_EPROTOCOL;
    }

    // Errors promise no chunks; their snapshot cursors are diagnostic only.
    if (event->Status != OS_EOK && event->Count) {
        return OS_EPROTOCOL;
    }
    if (event->Status == OS_EOK) {
        // A successful prefix must fit the snapshot and include every record
        // already seen in this drain's completion chunks.
        if (event->Through > event->Highest || event->Through < adapter->DrainSeenThrough) {
            return OS_EPROTOCOL;
        }
        // An empty prefix is valid only if the snapshot has no newer records.
        if (!event->Count && event->Highest != event->After) {
            return OS_EPROTOCOL;
        }
        // A repeated end marker cannot change an established snapshot.
        if (adapter->DrainEnded && (event->Through != adapter->DrainThrough ||
                                    event->Highest != adapter->DrainHighest)) {
            return OS_EPROTOCOL;
        }
    }
    
    // Save the successful snapshot bound and schedule another pass. The end
    // marker alone does not mean all chunks arrived: the scheduler completes
    // this drain only when local contiguous processing reaches DrainThrough.
    if (event->Status == OS_EOK) {
        adapter->DrainThrough = event->Through;
        adapter->DrainHighest = event->Highest;
        adapter->DrainEnded = true;
        adapter->LinkNeeded = true;
    }
    
    // Errors retain the deadline and retry count; a fresh attempt ID is
    // used after expiry, including busy and concurrently retired cursors.
    return OS_EOK;
}

/** 
 * @brief Validate the echoed ACK and its claimed retirement against what netd sent.
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
    
    // Its echoed cursors cannot name an ACK netd has not published, even if
    // the driver reports an error. Older cumulative ACK echoes are valid.
    if (event->Ack.through_batch_id > adapter->SentAck.through_batch_id) {
        return OS_EPROTOCOL;
    }
    if (event->Ack.through_completion_sequence > adapter->SentAck.through_completion_sequence) {
        return OS_EPROTOCOL;
    }
    
    // A successful response promises that both echoed prefixes were retired.
    // Progress is the confirmation of reuse, not the ACK echo itself.
    if (event->Status == OS_EOK &&
        event->Progress.retired_batch_id < event->Ack.through_batch_id) {
        return OS_EPROTOCOL;
    }
   
    if (event->Status == OS_EOK &&
        event->Progress.retired_completion_sequence < event->Ack.through_completion_sequence) {
        return OS_EPROTOCOL;
    }
    // A failed ACK does not confirm its echoed cursors; the snapshot may still
    // report earlier retirement. Preserve the failure status for recovery.
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
        adapter->CloseRequested || adapter->State == NET_ADAPTER_CLOSED ||
        adapter->State == NET_ADAPTER_QUARANTINED) {
        return OS_ENOENT;
    }
    
    if (event->Count > adapter->BatchSize || event->Status < OS_EOK ||
        event->Status >= __OS_ECOUNT || !__IsSessionProgressValid(adapter, &event->Progress)) {
        NetAdapterFail(adapter, OS_EPROTOCOL);
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
    if (!entry) {
        return;
    }
    
    if (count > NET_ADAPTER_BATCH_MAX) {
        NetAdapterProtocolError(entry->Adapter);
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

    status = __ValidateEvent(entry->Adapter, entry->Adapter->Driver, &event);
    if (status != OS_EOK) {
        return;
    }

    status = __HandleAdmission(entry->Adapter, &event, entry->Now);
    switch (status) {
        case OS_ENOENT:
            return;
        case OS_EOK:
            status = __HandleSessionProgress(entry->Adapter, &event->Progress);
        default:
            break;
    }
    if (status != OS_EOK) {
        NetAdapterFail(entry->Adapter, status);
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
        NetAdapterProtocolError(entry->Adapter);
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
    // A missing journal predecessor is recoverable; do not fail or
    // retire progress until the drain fills that gap.
    if (status == OS_EBUSY) {
        return;
    }

    switch (status) {
        case OS_ENOENT:
            return;
        case OS_EOK:
            status = __HandleSessionProgress(entry->Adapter, &event->Progress);
        default:
            break;
    }
    if (status != OS_EOK) {
        NetAdapterFail(entry->Adapter, status);
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
    oserr_t              status;
    
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

    status = __ValidateEvent(entry->Adapter, entry->Adapter->Driver, &event);
    if (status != OS_EOK) {
        return;
    }
    
    status = __HandleDrainEnd(entry->Adapter, &event);
    switch (status) {
        case OS_ENOENT:
            return;
        case OS_EOK:
            status = __HandleSessionProgress(entry->Adapter, &event->Progress);
        default:
            break;
    }
    if (status != OS_EOK) {
        NetAdapterFail(entry->Adapter, status);
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
    oserr_t              status;
    
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

    status = __ValidateEvent(entry->Adapter, entry->Adapter->Driver, &event);
    if (status != OS_EOK) {
        return;
    }
    
    status = __ValidateACK(entry->Adapter, &event);
    switch (status) {
        case OS_ENOENT:
            return;
        case OS_EOK:
            status = __HandleSessionProgress(entry->Adapter, &event->Progress);
        default:
            break;
    }
    if (status != OS_EOK) {
        NetAdapterFail(entry->Adapter, status);
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

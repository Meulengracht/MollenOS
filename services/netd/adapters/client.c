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
    (void)NetAdapterEvent(entry->Adapter, entry->Driver, &event, entry->Now);
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
    (void)NetAdapterEvent(entry->Adapter, entry->Driver, &event, entry->Now);
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
    (void)NetAdapterEvent(entry->Adapter, entry->Driver, &event, entry->Now);
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
    (void)NetAdapterEvent(entry->Adapter, entry->Driver, &event, entry->Now);
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

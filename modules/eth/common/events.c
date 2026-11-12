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

#include "netadapter-private.h"

// Send saved completions after the given sequence. Results stay in the journal
// until acknowledged, so the client can request them again if delivery fails.
void
EthernetPushCompletions(
    _In_ EthernetDevice_t* device,
    _In_ uint64_t          after)
{
    struct ctt_netadapter_completion records[ETHERNET_BATCH];
    EthernetSession_t*               session = &device->Common->Session;
    int                              status;
    uint32_t                         count;

    while (after < session->Progress.highest_completion_sequence) {
        status = EthernetReadCompletions(device, after, ETHERNET_BATCH, records, &count);
        if (status != OS_EOK) {
            break;
        }
        after += count;

        // If sending fails, leave the results in the journal for the client to read.
        status = ctt_netadapter_event_completions_single(
            EthernetServer(),
            session->Owner,
            &session->Identity,
            0, // Request zero identifies an unsolicited completion push.
            records,
            count,
            &session->Progress
        );
        if (status) {
            TRACE("EthernetPushCompletions: device %u send failed", device->Common->DeviceId);
            break;
        }
    }
}

void
EthernetSendFault(
    _In_ EthernetDevice_t* device,
    _In_ oserr_t           status)
{
    struct ctt_netadapter_fault fault = {
        .status = status
    };

    if (ctt_netadapter_event_fault_single(
        EthernetServer(),
        device->Common->Session.Owner,
        &device->Common->Session.Identity,
        &fault
    )) {
        TRACE("EthernetFault: device %u fault notification failed", device->Common->DeviceId);
    }
}

void
EthernetSendLink(
    _In_ EthernetDevice_t* device)
{
    if (ctt_netadapter_event_link_changed_single(
        EthernetServer(),
        device->Common->Session.Owner,
        &device->Common->Session.Identity,
        &device->Common->Link
    )) {
        TRACE("EthernetReadConfiguration: device %u link notification failed", device->Common->DeviceId);
    }
}

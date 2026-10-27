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
 */

#include <ddk/utils.h>
#include <os/usched/job.h>
#include <string.h>

#include "hub.h"

static void
__HandleEvents(
    _In_ void* context,
    _In_ void* cancellationToken)
{
    HubDevice_t* hubDevice = context;
    uint8_t      changes[32];
    bool         stalled;

    _CRT_UNUSED(cancellationToken);
    
    if (hubDevice->Events.State == HUB_EVENTS_INITIAL_SCAN) {
        hubDevice->Events.State = HUB_EVENTS_RUNNING;
        HubEnumeratePorts(hubDevice);
    }

    while (hubDevice->Events.Pending && hubDevice->Events.State == HUB_EVENTS_RUNNING) {
        // Take a snapshot before any control request yields to the event pump.
        memcpy(changes, hubDevice->Events.Changes, sizeof(changes));
        memset(hubDevice->Events.Changes, 0, sizeof(changes));
        
        stalled = hubDevice->Events.Stalled;
        hubDevice->Events.Stalled = false;
        hubDevice->Events.Pending = false;

        if (stalled) {
            UsbClearFeature(&hubDevice->Base->DeviceContext, USBPACKET_DIRECTION_ENDPOINT,
                USB_ENDPOINT_ADDRESS(hubDevice->Interrupt->Address), USB_FEATURE_HALT);
            UsbEndpointReset(&hubDevice->Base->DeviceContext,
                USB_ENDPOINT_ADDRESS(hubDevice->Interrupt->Address));
            UsbTransferResetPeriodic(&hubDevice->Base->DeviceContext, hubDevice->TransferId);
        }
        HubInterrupt(hubDevice, changes);
    }
    hubDevice->Events.Job = UUID_INVALID;
}

static void
__ScheduleEvents(
    _In_ HubDevice_t* hubDevice)
{
    if (hubDevice->Events.Job == UUID_INVALID) {
        hubDevice->Events.Job = usched_job_queue(__HandleEvents, hubDevice);
        if (hubDevice->Events.Job == UUID_INVALID) {
            ERROR("__ScheduleEvents failed to schedule hub status work");
        }
    }
}

void
HubEventsStart(
    _In_ HubDevice_t* hubDevice)
{
    hubDevice->Events.State = HUB_EVENTS_INITIAL_SCAN;
    __ScheduleEvents(hubDevice);
}

void
HubEventsQueue(
    _In_ HubDevice_t* hubDevice,
    _In_ bool         stalled,
    _In_ size_t       dataIndex)
{
    const uint8_t* changes;
    size_t         length = (hubDevice->PortCount + 8U) / 8U;
    size_t         i;

    if (hubDevice->Events.State == HUB_EVENTS_STOPPING) {
        return;
    }

    if (stalled) {
        hubDevice->Events.Stalled = true;
    } else {
        if (dataIndex > 0x100 - length) {
            return;
        }
        
        changes = (const uint8_t*)hubDevice->Buffer + dataIndex;
        for (i = 0; i < length; i++) {
            hubDevice->Events.Changes[i] |= changes[i];
        }
    }
    hubDevice->Events.Pending = true;

    // Client callbacks must return so the receive loop can deliver RPC replies.
    // Coalesce repeated bitmaps while the one port worker is awaiting a reply.
    __ScheduleEvents(hubDevice);
}

void
HubEventsStop(
    _In_ HubDevice_t* hubDevice)
{
    hubDevice->Events.State = HUB_EVENTS_STOPPING;
    if (hubDevice->Events.Job != UUID_INVALID) {
        int exitCode;
        usched_job_join(hubDevice->Events.Job, &exitCode);
    }
}

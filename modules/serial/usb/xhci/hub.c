/**
 * Copyright 2026, Philip Meulengracht
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

#define __TRACE

#include <ddk/barrier.h>
#include <ddk/utils.h>
#include <string.h>
#include <threads.h>

#include "xhci.h"

void
XhciHubConfigurationComplete(
    _In_ XhciController_t* controller,
    _In_ XhciDevice_t*     device,
    _In_ unsigned int      completionCode)
{
    _CRT_UNUSED(controller);

    TRACE("XHCI hub configure complete: slot=%u code=%u", device->SlotId, completionCode);
    device->HubConfiguration.Status = completionCode == XHCI_TRB_COMPLETION_SUCCESS ?
        OS_EOK : OS_EDEVFAULT;
    device->HubConfiguration.Pending = false;
}

oserr_t
HCIConfigureHub(
    _In_ UsbManagerController_t* controllerBase,
    _In_ uint8_t                 hubAddress,
    _In_ uint8_t                 portCount,
    _In_ uint16_t                characteristics)
{
    XhciController_t* controller = (XhciController_t*)controllerBase;
    XhciDevice_t*     hub = NULL;
    XhciTrb_t         trb = { 0 };
    reg32_t*          slot;
    unsigned int      i;
    oserr_t           status;

    if (hubAddress == 0 || portCount == 0) {
        return OS_EINVALPARAMS;
    }

    if (controllerBase->State.Phase != USB_CONTROLLER_READY ||
        controller->State != XHCI_CONTROLLER_RUNNING) {
        return OS_EBUSY;
    }

    foreach(node, &controller->Devices) {
        if (((XhciDevice_t*)node->value)->UsbAddress == hubAddress) {
            hub = node->value;
            break;
        }
    }
    if (hub == NULL) {
        return OS_ENOENT;
    }

    if (hub->State != XHCI_DEVICE_ADDRESSED || hub->HubConfiguration.Waiting ||
        hub->HubConfiguration.Pending) {
        return OS_EBUSY;
    }

    // InputContext is shared with address and endpoint configuration commands.
    for (i = 0; i < XHCI_COMMAND_RING_ENTRIES; i++) {
        if (controller->Commands[i].Device == hub) {
            return OS_EBUSY;
        }
    }

    memset(hub->InputContext, 0, 33 * controller->ContextSize);
    ((reg32_t*)hub->InputContext)[1] = 1; // Add only the slot context; leave EP0 unchanged.
    slot = (reg32_t*)(hub->InputContext + controller->ContextSize);
    dma_rmb();

    memcpy(slot, hub->DeviceContext, controller->ContextSize);
    slot[0] |= XHCI_SLOT_CONTEXT_HUB;
    // The driver uses alternate setting zero (single TT). The descriptor's
    // compound-device bit describes packaging, not multiple transaction translators.
    slot[0] &= ~XHCI_SLOT_CONTEXT_MTT;
    if (controller->CapRegisters->HciVersion > 0x95) {
        slot[1] &= ~XHCI_SLOT_CONTEXT_NUM_PORTS(0xFFU);
        slot[1] |= XHCI_SLOT_CONTEXT_NUM_PORTS(portCount);
        slot[2] &= ~XHCI_SLOT_CONTEXT_TT_THINK(3);
        if (hub->Speed == USBSPEED_HIGH) {
            slot[2] |= XHCI_SLOT_CONTEXT_TT_THINK((characteristics >> 5) & 3);
        }
    }
    slot[3] = 0; // Address and slot state are output-only fields.

    // xHCI 1.x updates hub fields with Configure Endpoint. Evaluate Context is
    // the older 0.95 operation and does not update the same fields on 1.x.
    trb.Parameter = hub->InputContextDeviceAddress;
    trb.Control = XHCI_TRB_CONTROL_SLOT_ID(hub->SlotId);
    if (controller->CapRegisters->HciVersion > 0x95) {
        trb.Control |= XHCI_TRB_CONTROL_TYPE(XHCI_TRB_TYPE_CONFIGURE_ENDPOINT);
    } else {
        trb.Control |= XHCI_TRB_CONTROL_TYPE(XHCI_TRB_TYPE_EVALUATE_CONTEXT);
    }

    hub->HubConfiguration.Pending = true;
    hub->HubConfiguration.Waiting = true;
    hub->HubConfiguration.Status = OS_EINCOMPLETE;
    controllerBase->State.OperationsInFlight++;
    dma_wmb();
    
    status = XhciCommandSubmit(
        controller,
        hub,
        NULL,
        XHCI_COMMAND_CONFIGURE_HUB,
        &trb
    );
    if (status != OS_EOK) {
        hub->HubConfiguration.Pending = false;
        goto done;
    }
    TRACE("XHCI hub configure submitted: slot=%u command=%u ports=%u",
        hub->SlotId, (trb.Control >> 10) & 0x3F, portCount);

    // Yield so the normal interrupt callback can process the command event.
    // Keep both objects alive across yields, including removal and timeout.
    for (i = 0; i < 100; i++) {
        if (!hub->HubConfiguration.Pending) {
            break;
        }
        if (hub->State != XHCI_DEVICE_ADDRESSED ||
            controllerBase->State.Phase != USB_CONTROLLER_READY) {
            break;
        }
        if (thrd_sleep(&(struct timespec) { .tv_nsec = 10 * NSEC_PER_MSEC }, NULL) != 0) {
            break;
        }
    }

    status = hub->HubConfiguration.Pending ? OS_ETIMEOUT : hub->HubConfiguration.Status;
    if (hub->State != XHCI_DEVICE_ADDRESSED ||
        controllerBase->State.Phase != USB_CONTROLLER_READY) {
        status = OS_EDEVFAULT;
    }
    
    if (status != OS_EOK && hub->State == XHCI_DEVICE_ADDRESSED) {
        // A timed-out command may still read InputContext. Block new transfers
        // until detach has stopped the slot; never recycle its DMA storage here.
        hub->State = XHCI_DEVICE_FAILED;
    }

done:
    TRACE("XHCI hub configure result: slot=%u status=%u pending=%u",
        hub->SlotId, status, hub->HubConfiguration.Pending);
    hub->HubConfiguration.Waiting = false;
    controllerBase->State.OperationsInFlight--;
    return status;
}

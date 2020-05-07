/* MollenOS
 *
 * Copyright 2011, Philip Meulengracht
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
 * along with this program.If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * MollenOS MCore - Open Host Controller Interface Driver
 * TODO:
 *    - Power Management
 */
//#define __TRACE

/* Includes 
 * - System */
#include <os/mollenos.h>
#include <ddk/utils.h>
#include "ohci.h"

/* Includes
 * - Library */
#include <ds/collection.h>
#include <assert.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>

/* OhciErrorMessages
 * Textual representations of the possible error codes */
const char *OhciErrorMessages[] = {
    "No Error",
    "CRC Error",
    "Bit Stuffing Violation",
    "Data Toggle Mismatch",
    "Stall PID recieved",
    "Device Not Responding",
    "PID Check Failure",
    "Unexpected PID",
    "Data Overrun",
    "Data Underrun",
    "Reserved",
    "Reserved",
    "Buffer Overrun",
    "Buffer Underrun",
    "Not Accessed",
    "Not Accessed"
};

/* OhciQueueResetInternalData
 * Removes and cleans up any existing transfers, then reinitializes. */
OsStatus_t
OhciQueueResetInternalData(
    _In_ OhciController_t*          Controller)
{
    // Variables
    OhciIsocTransferDescriptor_t *NulliTd   = NULL;
    OhciTransferDescriptor_t *NullTd        = NULL;
    OhciQueueHead_t *NullQh                 = NULL;
    uintptr_t NullQhPhysical                = 0;
    uintptr_t NullTdPhysical                = 0;
    uintptr_t NulliTdPhysical               = 0;
    
    // Debug
    TRACE("OhciQueueResetInternalData()");

    // Reset all tds and qhs
    UsbSchedulerResetInternalData(Controller->Base.Scheduler, 1, 1);
    UsbSchedulerGetPoolElement(Controller->Base.Scheduler, OHCI_QH_POOL, 
        OHCI_QH_NULL, (uint8_t**)&NullQh, &NullQhPhysical);
    UsbSchedulerGetPoolElement(Controller->Base.Scheduler, OHCI_TD_POOL, 
        OHCI_TD_NULL, (uint8_t**)&NullTd, &NullTdPhysical);
    UsbSchedulerGetPoolElement(Controller->Base.Scheduler, OHCI_iTD_POOL, 
        OHCI_iTD_NULL, (uint8_t**)&NulliTd, &NulliTdPhysical);
    
    // Reset indexes
    Controller->TransactionQueueBulkIndex    = USB_ELEMENT_NO_INDEX;
    Controller->TransactionQueueControlIndex = USB_ELEMENT_NO_INDEX;

    // Initialize the null-qh
    NullQh->Flags       = OHCI_QH_LOWSPEED | OHCI_QH_IN | OHCI_QH_SKIP;
    NullQh->EndPointer  = NullTdPhysical;
    NullQh->Current     = NullTdPhysical;
    NullQh->LinkPointer = OHCI_LINK_HALTED;

    // Initialize the null-td
    NullTd->Flags     = OHCI_TD_IN | OHCI_TD_IOC_NONE;
    NullTd->BufferEnd = 0;
    NullTd->Cbp       = 0;
    NullTd->Link      = OHCI_LINK_HALTED;

    // Initialize the null-isoc td
    NulliTd->Flags     = OHCI_TD_IN | OHCI_TD_IOC_NONE;
    NulliTd->BufferEnd = 0;
    NulliTd->Cbp       = 0;
    NulliTd->Link      = OHCI_LINK_HALTED;
    return OsSuccess;
}

OsStatus_t
OhciQueueInitialize(
    _In_ OhciController_t* Controller)
{
    UsbSchedulerSettings_t Settings;

    TRACE("OhciQueueInitialize()");

    TRACE(" > Configuring scheduler");
    UsbSchedulerSettingsCreate(&Settings, OHCI_FRAMELIST_SIZE, 1, 900, USB_SCHEDULER_NULL_ELEMENT);

    UsbSchedulerSettingsConfigureFrameList(&Settings, (reg32_t*)&Controller->Hcca->InterruptTable[0],
        Controller->HccaDMATable.entries[0].address + offsetof(OhciHCCA_t, InterruptTable));

    UsbSchedulerSettingsAddPool(&Settings, sizeof(OhciQueueHead_t), OHCI_QH_ALIGNMENT, OHCI_QH_COUNT, 
        OHCI_QH_START, offsetof(OhciQueueHead_t, LinkPointer), 
        offsetof(OhciQueueHead_t, Current), offsetof(OhciQueueHead_t, Object));
    
    UsbSchedulerSettingsAddPool(&Settings, sizeof(OhciTransferDescriptor_t), OHCI_TD_ALIGNMENT, OHCI_TD_COUNT, 
        OHCI_TD_START, offsetof(OhciTransferDescriptor_t, Link), 
        offsetof(OhciTransferDescriptor_t, Link), offsetof(OhciTransferDescriptor_t, Object));
    
    UsbSchedulerSettingsAddPool(&Settings, sizeof(OhciIsocTransferDescriptor_t), OHCI_iTD_ALIGNMENT, OHCI_iTD_COUNT, 
        OHCI_iTD_START, offsetof(OhciIsocTransferDescriptor_t, Link), 
        offsetof(OhciIsocTransferDescriptor_t, Link), offsetof(OhciIsocTransferDescriptor_t, Object));
    
    // Create the scheduler
    TRACE(" > Initializing scheduler");
    UsbSchedulerInitialize(&Settings, &Controller->Base.Scheduler);

    // Initialize internal data structures
    return OhciQueueResetInternalData(Controller);
}

OsStatus_t
OhciQueueReset(
    _In_ OhciController_t* Controller)
{
    TRACE("OhciQueueReset()");

    OhciSetMode(Controller, OHCI_CONTROL_SUSPEND);
    UsbManagerClearTransfers(&Controller->Base);
    return OhciQueueResetInternalData(Controller);
}

OsStatus_t
OhciQueueDestroy(
    _In_ OhciController_t* Controller)
{
    TRACE("OhciQueueDestroy()");

    OhciQueueReset(Controller);
    UsbSchedulerDestroy(Controller->Base.Scheduler);
    return OsSuccess;
}

UsbTransferStatus_t
OhciGetStatusCode(
    _In_ int ConditionCode)
{
    TRACE("[ohci] [error_code] %i", ConditionCode);
    if (ConditionCode == 0) {
        return TransferFinished;
    }
    else if (ConditionCode == 4) {
        return TransferStalled;
    }
    else if (ConditionCode == 3) {
        return TransferInvalidToggles;
    }
    else if (ConditionCode == 2 || ConditionCode == 1) {
        return TransferBabble;
    }
    else if (ConditionCode == 5) {
        return TransferNotResponding;
    }
    else if (ConditionCode == 15) {
        return TransferNotProcessed;
    }
    else {
        TRACE("[ohci] [error_code]: 0x%x (%s)", ConditionCode, OhciErrorMessages[ConditionCode]);
        return TransferInvalid;
    }
}

/* HciProcessElement 
 * Proceses the element accordingly to the reason given. The transfer associated
 * will be provided in <Context> */
int
HciProcessElement(
    _In_ UsbManagerController_t*    Controller,
    _In_ uint8_t*                   Element,
    _In_ int                        Reason,
    _In_ void*                      Context)
{
    UsbManagerTransfer_t *Transfer  = (UsbManagerTransfer_t*)Context;

    // Debug
    TRACE("OhciProcessElement(Reason %i)", Reason);

    // Handle the reasons
    switch (Reason) {
        case USB_REASON_DUMP: {
            if (Element == (uint8_t*)Transfer->EndpointDescriptor) {
                OhciQhDump((OhciController_t*)Controller, (OhciQueueHead_t*)Element);
            }
            else if (Transfer->Transfer.Type != USB_TRANSFER_ISOCHRONOUS) {
                OhciTdDump((OhciController_t*)Controller, (OhciTransferDescriptor_t*)Element);
            }
            else {
                OhciiTdDump((OhciController_t*)Controller, (OhciIsocTransferDescriptor_t*)Element);
            }
        } break;
        
        case USB_REASON_SCAN: {
            if (Element == (uint8_t*)Transfer->EndpointDescriptor) {
                return ITERATOR_CONTINUE; // Skip scan on queue-heads
            }

            if (Transfer->Transfer.Type != USB_TRANSFER_ISOCHRONOUS) {
                OhciTdValidate(Transfer, (OhciTransferDescriptor_t*)Element);
                if (Transfer->Flags & TransferFlagShort) {
                    return ITERATOR_STOP; // Stop here
                }
            }
            else {
                OhciiTdValidate(Transfer, (OhciIsocTransferDescriptor_t*)Element);
            }
        } break;
        
        case USB_REASON_RESET: {
            if (Element != (uint8_t*)Transfer->EndpointDescriptor) {
                if (Transfer->Transfer.Type != USB_TRANSFER_ISOCHRONOUS) {
                    OhciTdRestart((OhciController_t*)Controller, Transfer, (OhciTransferDescriptor_t*)Element);
                }
                else {
                    OhciiTdRestart((OhciController_t*)Controller, Transfer, (OhciIsocTransferDescriptor_t*)Element);
                }
            }
        } break;
        
        case USB_REASON_FIXTOGGLE: {
            if (Element == (uint8_t*)Transfer->EndpointDescriptor) {
                return ITERATOR_CONTINUE; // Skip sync on queue-heads
            }

            // Isochronous transfers don't use toggles.
            if (Transfer->Transfer.Type != USB_TRANSFER_ISOCHRONOUS) {
                OhciTdSynchronize(Transfer, (OhciTransferDescriptor_t*)Element);
            }
        } break;

        case USB_REASON_LINK: {
            // If it's a queue head link that, otherwise ignore
            if (Element == (uint8_t*)Transfer->EndpointDescriptor) {
                if (Transfer->Transfer.Type == USB_TRANSFER_CONTROL || Transfer->Transfer.Type == USB_TRANSFER_BULK) {
                    OhciQhLink((OhciController_t*)Controller, Transfer->Transfer.Type, (OhciQueueHead_t*)Element);
                }
                else {
                    UsbSchedulerLinkPeriodicElement(Controller->Scheduler, OHCI_QH_POOL, Element);
                }
                return ITERATOR_STOP;
            }
        } break;
        
        case USB_REASON_UNLINK: {
            // If it's a queue head unlink that, otherwise ignore
            if (Element == (uint8_t*)Transfer->EndpointDescriptor) {
                if (Transfer->Transfer.Type == USB_TRANSFER_INTERRUPT || Transfer->Transfer.Type == USB_TRANSFER_ISOCHRONOUS) {
                    UsbSchedulerUnlinkPeriodicElement(Controller->Scheduler, OHCI_QH_POOL, Element);
                }
                return ITERATOR_STOP;
            }
        } break;
        
        case USB_REASON_CLEANUP: {
            // Very simple cleanup
            UsbSchedulerFreeElement(Controller->Scheduler, Element);
        } break;
    }
    return ITERATOR_CONTINUE;
}

/* HciProcessEvent
 * Invoked on different very specific events that require assistance. If a transfer 
 * associated will be provided in <Context> */
void
HciProcessEvent(
    _In_ UsbManagerController_t*    Controller,
    _In_ int                        Event,
    _In_ void*                      Context)
{
    // Variables
    UsbManagerTransfer_t *Transfer  = (UsbManagerTransfer_t*)Context;

    // Debug
    TRACE("OhciProcessEvent(Event %i)", Event);

    // Handle the reasons
    switch (Event) {
        case USB_EVENT_RESTART_DONE: {
            OhciQhRestart((OhciController_t*)Controller, Transfer);
        } break;
    }
}

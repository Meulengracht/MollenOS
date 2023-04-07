/**
 * MollenOS
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
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * Open Host Controller Interface Driver
 * TODO:
 *    - Power Management
 */
//#define __TRACE

#include <assert.h>
#include <ddk/utils.h>
#include <os/mollenos.h>
#include "ohci.h"
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

static oserr_t
__ResetInternalData(
    _In_ OhciController_t* controller)
{
    OhciIsocTransferDescriptor_t* nullITD         = NULL;
    OhciTransferDescriptor_t*     nullTD          = NULL;
    OhciQueueHead_t*              nullQH          = NULL;
    uintptr_t                     nullQHPhysical  = 0;
    uintptr_t                     nullTDPhysical  = 0;
    uintptr_t                     nullITDPhysical = 0;
    
    TRACE("__ResetInternalData()");

    // Reset all tds and qhs
    UsbSchedulerResetInternalData(controller->Base.Scheduler, 1, 1);
    UsbSchedulerGetPoolElement(controller->Base.Scheduler, OHCI_QH_POOL,
                               OHCI_QH_NULL, (uint8_t**)&nullQH, &nullQHPhysical);
    UsbSchedulerGetPoolElement(controller->Base.Scheduler, OHCI_TD_POOL,
                               OHCI_TD_NULL, (uint8_t**)&nullTD, &nullTDPhysical);
    UsbSchedulerGetPoolElement(controller->Base.Scheduler, OHCI_iTD_POOL,
                               OHCI_iTD_NULL, (uint8_t**)&nullITD, &nullITDPhysical);
    
    // Reset indexes
    controller->TransactionQueueBulkIndex    = USB_ELEMENT_NO_INDEX;
    controller->TransactionQueueControlIndex = USB_ELEMENT_NO_INDEX;

    // Initialize the null-qh
    nullQH->Flags       = OHCI_QH_LOWSPEED | OHCI_QH_IN | OHCI_QH_SKIP;
    nullQH->EndPointer  = nullTDPhysical;
    nullQH->Current     = nullTDPhysical;
    nullQH->LinkPointer = OHCI_LINK_HALTED;

    // Initialize the null-td
    nullTD->Flags     = OHCI_TD_IN | OHCI_TD_IOC_NONE;
    nullTD->BufferEnd = 0;
    nullTD->Cbp       = 0;
    nullTD->Link      = OHCI_LINK_HALTED;

    // Initialize the null-isoc td
    nullITD->Flags     = OHCI_TD_IN | OHCI_TD_IOC_NONE;
    nullITD->BufferEnd = 0;
    nullITD->Cbp       = 0;
    nullITD->Link      = OHCI_LINK_HALTED;
    return OS_EOK;
}

oserr_t
OHCIQueueInitialize(
    _In_ OhciController_t* Controller)
{
    UsbSchedulerSettings_t Settings;

    TRACE("OHCIQueueInitialize()");

    TRACE(" > Configuring scheduler");
    UsbSchedulerSettingsCreate(&Settings, OHCI_FRAMELIST_SIZE, 1, 900, USB_SCHEDULER_NULL_ELEMENT);

    UsbSchedulerSettingsConfigureFrameList(&Settings, (reg32_t*)&Controller->Hcca->InterruptTable[0],
        Controller->HccaDMATable.Entries[0].Address + offsetof(OhciHCCA_t, InterruptTable));

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
    return __ResetInternalData(Controller);
}

oserr_t
OHCIQueueReset(
    _In_ OhciController_t* controller)
{
    TRACE("OHCIQueueReset()");

    OhciSetMode(controller, OHCI_CONTROL_SUSPEND);
    UsbManagerClearTransfers(&controller->Base);
    return __ResetInternalData(controller);
}

oserr_t
OhciQueueDestroy(
    _In_ OhciController_t* Controller)
{
    TRACE("OhciQueueDestroy()");

    OHCIQueueReset(Controller);
    UsbSchedulerDestroy(Controller->Base.Scheduler);
    return OS_EOK;
}

enum USBTransferCode
OhciGetStatusCode(
    _In_ int ConditionCode)
{
    TRACE("[ohci] [error_code] %i", ConditionCode);
    if (ConditionCode == 0) {
        // Executed successfully
        return TransferOK;
    } else if (ConditionCode == 4) {
        return TransferStalled;
    } else if (ConditionCode == 3) {
        return TransferInvalidToggles;
    } else if (ConditionCode == 2 || ConditionCode == 1) {
        return TransferBabble;
    } else if (ConditionCode == 5) {
        return TransferNotResponding;
    } else if (ConditionCode == 15) {
        // Not executed
        return TransferOK;
    } else {
        TRACE("[ohci] [error_code]: 0x%x (%s)", ConditionCode, OhciErrorMessages[ConditionCode]);
        return TransferInvalid;
    }
}

bool
HCIProcessElement(
    _In_ UsbManagerController_t*    controller,
    _In_ uint8_t*                   element,
    _In_ enum HCIProcessReason      reason,
    _In_ void*                      context)
{
    UsbManagerTransfer_t* transfer = context;

    TRACE("OhciProcessElement(reason=%i)", reason);

    // Handle the reasons
    switch (reason) {
        case HCIPROCESS_REASON_DUMP: {
            if (element == (uint8_t*)transfer->EndpointDescriptor) {
                OHCIQHDump((OhciController_t*)controller, (OhciQueueHead_t*)element);
            } else if (transfer->Type != USBTRANSFER_TYPE_ISOC) {
                OHCITDDump((OhciController_t*)controller, (OhciTransferDescriptor_t*)element);
            } else {
                OHCIITDDump((OhciController_t*)controller, (OhciIsocTransferDescriptor_t*)element);
            }
        } break;
        
        case HCIPROCESS_REASON_SCAN: {
            if (element == (uint8_t*)transfer->EndpointDescriptor) {
                return true; // Skip scan on queue-heads
            }

            if (transfer->Type != USBTRANSFER_TYPE_ISOC) {
                OHCITDVerify(transfer, (OhciTransferDescriptor_t*)element);
                if (transfer->Flags & __USBTRANSFER_FLAG_SHORT) {
                    return false; // Stop here
                }
            } else {
                OHCIITDVerify(transfer, (OhciIsocTransferDescriptor_t*)element);
            }
        } break;
        
        case HCIPROCESS_REASON_RESET: {
            if (element != (uint8_t*)transfer->EndpointDescriptor) {
                if (transfer->Type != USBTRANSFER_TYPE_ISOC) {
                    OHCITDRestart((OhciController_t*)controller, transfer, (OhciTransferDescriptor_t*)element);
                } else {
                    OHCIITDRestart((OhciController_t*)controller, transfer, (OhciIsocTransferDescriptor_t*)element);
                }
            }
        } break;
        
        case HCIPROCESS_REASON_FIXTOGGLE: {
            if (element == (uint8_t*)transfer->EndpointDescriptor) {
                return true; // Skip sync on queue-heads
            }

            // Isochronous transfers don't use toggles.
            if (transfer->Type != USBTRANSFER_TYPE_ISOC) {
                OhciTdSynchronize(transfer, (OhciTransferDescriptor_t*)element);
            }
        } break;

        case HCIPROCESS_REASON_LINK: {
            // If it's a queue head link that, otherwise ignore
            if (element == (uint8_t*)transfer->EndpointDescriptor) {
                if (__Transfer_IsAsync(transfer)) {
                    OhciQhLink((OhciController_t*)controller, transfer->Type, (OhciQueueHead_t*)element);
                } else {
                    UsbSchedulerLinkPeriodicElement(controller->Scheduler, OHCI_QH_POOL, element);
                }
                return false;
            }
        } break;
        
        case HCIPROCESS_REASON_UNLINK: {
            // If it's a queue head unlink that, otherwise ignore
            if (element == (uint8_t*)transfer->EndpointDescriptor) {
                if (__Transfer_IsPeriodic(transfer)) {
                    UsbSchedulerUnlinkPeriodicElement(controller->Scheduler, OHCI_QH_POOL, element);
                }
                return false;
            }
        } break;
        
        case HCIPROCESS_REASON_CLEANUP: {
            // Very simple cleanup
            UsbSchedulerFreeElement(controller->Scheduler, element);
        } break;

        default:
            break;
    }
    return true;
}

void
HCIProcessEvent(
        _In_ UsbManagerController_t* controller,
        _In_ enum HCIProcessEvent    event,
        _In_ void*                   context)
{
    UsbManagerTransfer_t* transfer = context;

    TRACE("HCIProcessEvent(event=%i)", Event);

    // Handle the reasons
    switch (event) {
        case HCIPROCESS_EVENT_RESET_DONE: {
            OhciQhRestart((OhciController_t*)controller, transfer);
        } break;
    }
}

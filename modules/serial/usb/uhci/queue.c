/**
 * Copyright 2023, Philip Meulengracht
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

//#define __TRACE

#include <os/mollenos.h>
#include <ddk/utils.h>
#include "uhci.h"
#include <stdlib.h>
#include <assert.h>
#include <string.h>

const char* g_transferDescriptions[] = {
    "No Error",
    "Bitstuff Error",
    "CRC/Timeout Error",
    "NAK Recieved",
    "Babble Detected",
    "Data Buffer Error",
    "Stalled",
    "Active"
};

static int
__CalculateFFS(
    _In_ size_t value)
{
    int set = 0;

    // Count the consecutive zero bits (trailing) on the right by binary search
    if (!(value & 0xFFFF)) { // 16 Bits
        set += 16;
        value >>= 16;
    }
    if (!(value & 0xFF)) { // 8 Bits
        set += 8;
        value >>= 8;
    }
    if (!(value & 0xF)) { // 4 Bits
        set += 4;
        value >>= 4;
    }
    if (!(value & 0x3)) { // 2 Bits
        set += 2;
        value >>= 2;
    }
    if (!(value & 0x1)) { // 1 Bit
        set++;
    }
    return set;
}

static int
__CalculateInterruptIndex(
    _In_ size_t frame)
{
    int index = 8 - __CalculateFFS(frame | UHCI_NUM_FRAMES);

    // If we are out of bounds then assume async queue
    if (index < 2 || index > 8) {
        index = UHCI_POOL_QH_ASYNC;
    }
    return index;
}

enum USBTransferCode
UHCIErrorCodeToTransferStatus(
    _In_ int conditionCode)
{
    TRACE("UHCIErrorCodeToTransferStatus(conditionCode=%i)", conditionCode);
    if (conditionCode == 0) {
        return USBTRANSFERCODE_SUCCESS;
    } else if (conditionCode == 6) {
        return USBTRANSFERCODE_STALL;
    } else if (conditionCode == 1) {
        return USBTRANSFERCODE_DATATOGGLEMISMATCH;
    } else if (conditionCode == 2) {
        return TransferNotResponding;
    } else if (conditionCode == 3) {
        return TransferNAK;
    } else if (conditionCode == 4) {
        return USBTRANSFERCODE_BABBLE;
    } else if (conditionCode == 5) {
        return TransferBufferError;
    } else {
        TRACE("UHCIErrorCodeToTransferStatus: %s", g_transferDescriptions[conditionCode]);
        return USBTRANSFERCODE_BABBLE;
    }
}

oserr_t
UhciQueueResetInternalData(
    _In_ UhciController_t* Controller)
{
    UhciTransferDescriptor_t* NullTd          = NULL;
    UhciQueueHead_t*          AsyncQh         = NULL;
    UhciQueueHead_t*          NullQh          = NULL;
    UhciQueueHead_t*          Qh              = NULL;
    uintptr_t                 AsyncQhPhysical = 0;
    uintptr_t                 NullQhPhysical  = 0;
    uintptr_t                 NullTdPhysical  = 0;
    int                       i;

    TRACE("UhciQueueResetInternalData()");

    // Reset all tds and qhs
    UsbSchedulerResetInternalData(Controller->Base.Scheduler, 1, 1);
    UsbSchedulerGetPoolElement(Controller->Base.Scheduler, UHCI_QH_POOL, 
        UHCI_POOL_QH_NULL, (uint8_t**)&NullQh, &NullQhPhysical);
    UsbSchedulerGetPoolElement(Controller->Base.Scheduler, UHCI_QH_POOL, 
        UHCI_POOL_QH_ASYNC, (uint8_t**)&AsyncQh, &AsyncQhPhysical);
    UsbSchedulerGetPoolElement(Controller->Base.Scheduler, UHCI_TD_POOL, 
        UHCI_POOL_TD_NULL, (uint8_t**)&NullTd, &NullTdPhysical);

    // Initialize interrupt-queue
    for (i = UHCI_POOL_QH_ISOCHRONOUS + 1; i < UHCI_POOL_QH_ASYNC; i++) {
        UsbSchedulerGetPoolElement(Controller->Base.Scheduler, UHCI_QH_POOL, i, (uint8_t**)&Qh, NULL);

        // All interrupts queues need to end in the async-head
        Qh->Queue               = (uint8_t)(i & 0xFF);
        Qh->Link                = (AsyncQhPhysical | UHCI_LINK_QH);
        Qh->Child               = NullTdPhysical | UHCI_LINK_END;
        Qh->Object.BreathIndex  = USB_ELEMENT_CREATE_INDEX(UHCI_QH_POOL, UHCI_POOL_QH_ASYNC);
        Qh->Object.DepthIndex   = USB_ELEMENT_NO_INDEX;
        Qh->Object.Flags        |= UHCI_LINK_QH;
    }

    // Initialize the null QH
    NullQh->Queue               = UHCI_POOL_QH_NULL;
    NullQh->Link                = (NullQhPhysical | UHCI_LINK_QH);
    NullQh->Child               = NullTdPhysical | UHCI_LINK_END;
    NullQh->Object.BreathIndex  = USB_ELEMENT_CREATE_INDEX(UHCI_QH_POOL, UHCI_POOL_QH_NULL);
    NullQh->Object.DepthIndex   = USB_ELEMENT_CREATE_INDEX(UHCI_TD_POOL, UHCI_POOL_TD_NULL);
    NullQh->Object.Flags        |= UHCI_LINK_QH;

    // Initialize the async QH
    AsyncQh->Queue              = UHCI_POOL_QH_ASYNC;
    AsyncQh->Link               = UHCI_LINK_END;
    AsyncQh->Child              = NullTdPhysical | UHCI_LINK_END;
    AsyncQh->Object.BreathIndex = USB_ELEMENT_NO_INDEX;
    AsyncQh->Object.DepthIndex  = USB_ELEMENT_CREATE_INDEX(UHCI_TD_POOL, UHCI_POOL_TD_NULL);
    AsyncQh->Object.Flags       |= UHCI_LINK_QH;
    
    // Initialize null-td
    NullTd->Flags               = UHCI_TD_LOWSPEED;
    NullTd->Header              = (reg32_t)(UHCI_TD_PID_OUT | UHCI_TD_DEVICE_ADDR(0x7F) | UHCI_TD_MAX_LEN(0x7FF));
    NullTd->Link                = UHCI_LINK_END;
    NullTd->Object.BreathIndex  = USB_ELEMENT_NO_INDEX;
    NullTd->Object.DepthIndex   = USB_ELEMENT_NO_INDEX;
    
    // 1024 Entries
    // Set all entries to the 8 interrupt queues, and we
    // want them interleaved such that some queues get visited more than others
    for (i = 0; i < UHCI_NUM_FRAMES; i++) {
        int Index = __CalculateInterruptIndex((size_t) i);
        UsbSchedulerGetPoolElement(Controller->Base.Scheduler, UHCI_QH_POOL, 
            Index, (uint8_t**)&Qh, &AsyncQhPhysical);

        Controller->Base.Scheduler->VirtualFrameList[i]   = (uintptr_t)Qh;
        Controller->Base.Scheduler->Settings.FrameList[i] = AsyncQhPhysical | UHCI_LINK_QH;
    }
    return OS_EOK;
}

oserr_t
UhciQueueInitialize(
    _In_ UhciController_t* Controller)
{
    UsbSchedulerSettings_t Settings;

    // Debug
    TRACE("UhciQueueInitialize()");

    TRACE(" > Configuring scheduler");
    UsbSchedulerSettingsCreate(&Settings, UHCI_NUM_FRAMES, 1, 900, 
        USB_SCHEDULER_FRAMELIST | USB_SCHEDULER_LINK_BIT_EOL);

    UsbSchedulerSettingsAddPool(&Settings, sizeof(UhciQueueHead_t), UHCI_QH_ALIGNMENT, UHCI_QH_COUNT, 
        UHCI_POOL_QH_START, offsetof(UhciQueueHead_t, Link), 
        offsetof(UhciQueueHead_t, Child), offsetof(UhciQueueHead_t, Object));
    
    UsbSchedulerSettingsAddPool(&Settings, sizeof(UhciTransferDescriptor_t), UHCI_TD_ALIGNMENT, UHCI_TD_COUNT, 
        UHCI_POOL_TD_START, offsetof(UhciTransferDescriptor_t, Link), 
        offsetof(UhciTransferDescriptor_t, Link), offsetof(UhciTransferDescriptor_t, Object));
    
    TRACE(" > Initializing scheduler");
    UsbSchedulerInitialize(&Settings, &Controller->Base.Scheduler);
    return UhciQueueResetInternalData(Controller);
}

oserr_t
UhciQueueReset(
    _In_ UhciController_t* Controller)
{
    TRACE("UhciQueueReset()");

    // Stop Controller
    if (UhciStop(Controller) != OS_EOK) {
        ERROR("Failed to stop the controller");
        return OS_EUNKNOWN;
    }
    UsbManagerClearTransfers(&Controller->Base);
    return UhciQueueResetInternalData(Controller);
}

void
UHCIQueueDestroy(
    _In_ UhciController_t* controller)
{
    TRACE("UHCIQueueDestroy()");

    // Make sure everything is unscheduled, reset and clean
    UhciQueueReset(controller);
    UsbSchedulerDestroy(controller->Base.Scheduler);
}

// This should be called regularly to keep the stored frame relevant
void
UhciUpdateCurrentFrame(
    _In_ UhciController_t* controller)
{
    uint16_t frameNo;
    int      delta;

    // Read the current frame, and use the last read frame to calculate the delta
    // then add to current frame
    frameNo = UhciRead16(controller, UHCI_REGISTER_FRNUM);
    delta   = (frameNo - controller->Frame) & UHCI_FRAME_MASK;
    controller->Frame += delta;
}

int
UhciConditionCodeToIndex(
    _In_ int conditionCode)
{
    int counter = 0;
    int cc      = conditionCode;
    TRACE("UhciConditionCodeToIndex(conditionCode=%i)", conditionCode);

    // Keep bit-shifting and count which bit is set
    for (; cc != 0;) {
        counter++;
        cc >>= 1;
    }
    if (counter >= 8) {
        counter = 0;
    }
    return counter;
}

static bool
__ElementIsQH(
        _In_ UsbManagerTransfer_t* transfer,
        _In_ const uint8_t*        element) {
    return transfer->Type != USBTRANSFER_TYPE_ISOC && element == (uint8_t*)transfer->RootElement;
}

bool
HCIProcessElement(
        _In_ UsbManagerController_t* controller,
        _In_ uint8_t*                element,
        _In_ enum HCIProcessReason   reason,
        _In_ void*                   context)
{
    UhciTransferDescriptor_t* Td       = (UhciTransferDescriptor_t*)element;
    TRACE("UHCIProcessElement(reason=%i)", reason);

    switch (reason) {
        case HCIPROCESS_REASON_DUMP: {
            UsbManagerTransfer_t* transfer = (UsbManagerTransfer_t*)context;
            if (__ElementIsQH(transfer, element)) {
                UHCIQHDump((UhciController_t*)controller, (UhciQueueHead_t*)Td);
            } else {
                UHCITDDump((UhciController_t*)controller, Td);
            }
        } break;
        
        case HCIPROCESS_REASON_SCAN: {
            struct HCIProcessReasonScanContext* scanContext = context;

            // If we have a queue-head allocated skip it
            if (__ElementIsQH(scanContext->Transfer, element)) {
                return true; // Skip scan on queue-heads
            }

            // Perform validation and handle short transfers
            UHCITDVerify(scanContext, Td);
        } break;
        
        case HCIPROCESS_REASON_RESET: {
            UsbManagerTransfer_t* transfer = context;
            if (__ElementIsQH(transfer, element)) {
                return true; // Skip reset on queue-heads
            }
            UHCITDRestart((UhciController_t*)controller, transfer, Td);
        } break;

        case HCIPROCESS_REASON_LINK: {
            UsbManagerTransfer_t* transfer = (UsbManagerTransfer_t*)context;

            // If it's a queue head link that
            if (transfer->Type != USBTRANSFER_TYPE_ISOC) {
                UHCIQHLink((UhciController_t*)controller, (UhciQueueHead_t*)element);
                return false;
            } else {
                // Link all elements
                UsbSchedulerLinkPeriodicElement(controller->Scheduler, UHCI_TD_POOL, element);
            }
        } break;
        
        case HCIPROCESS_REASON_UNLINK: {
            UsbManagerTransfer_t* transfer = (UsbManagerTransfer_t*)context;
            // If it's a queue head unlink that
            if (transfer->Type != USBTRANSFER_TYPE_ISOC) {
                UHCIQHUnlink((UhciController_t*)controller, (UhciQueueHead_t*)element);
                return false;
            }
            else {
                // Link all elements
                UsbSchedulerUnlinkPeriodicElement(controller->Scheduler, UHCI_TD_POOL, element);
            }
        } break;
        
        case HCIPROCESS_REASON_CLEANUP: {
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
    UsbManagerTransfer_t* Transfer = context;
    TRACE("HCIProcessEvent(event=%i)", Event);

    switch (event) {
        case HCIPROCESS_EVENT_RESET_DONE: {
            if (Transfer->Type != USBTRANSFER_TYPE_ISOC) {
                UHCIQHRestart((UhciController_t*)controller, Transfer);
            }
        } break;

        default:
            break;
    }
}

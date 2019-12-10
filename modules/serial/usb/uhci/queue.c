/* MollenOS
 *
 * Copyright 2011 - 2017, Philip Meulengracht
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
 * MollenOS MCore - Universal Host Controller Interface Driver
 * Todo:
 * Power Management
 */
//#define __TRACE

#include <os/mollenos.h>
#include <ddk/utils.h>
#include "uhci.h"
#include <stdlib.h>
#include <assert.h>
#include <string.h>

const char* UhciErrorMessages[] = {
    "No Error",
    "Bitstuff Error",
    "CRC/Timeout Error",
    "NAK Recieved",
    "Babble Detected",
    "Data Buffer Error",
    "Stalled",
    "Active"
};

/* UhciFFS
 * This function calculates the first free set of bits in a value */
size_t
UhciFFS(
    _In_ size_t Value)
{
    size_t Set = 0;

    if (!(Value & 0xFFFF)) { // 16 Bits
        Set += 16;
        Value >>= 16;
    }
    if (!(Value & 0xFF)) { // 8 Bits
        Set += 8;
        Value >>= 8;
    }
    if (!(Value & 0xF)) { // 4 Bits
        Set += 4;
        Value >>= 4;
    }
    if (!(Value & 0x3)) { // 2 Bits
        Set += 2;
        Value >>= 2;
    }
    if (!(Value & 0x1)) { // 1 Bit
        Set++;
    }
    return Set;
}

int
UhciDetermineInterruptIndex(
    _In_ size_t Frame)
{
    int Index = 8 - UhciFFS(Frame | UHCI_NUM_FRAMES);

    // If we are out of bounds then assume async queue
    if (Index < 2 || Index > 8) {
        Index = UHCI_POOL_QH_ASYNC;
    }
    return Index;
}

UsbTransferStatus_t
UhciGetStatusCode(
    _In_ int ConditionCode)
{
    if (ConditionCode == 0) {
        return TransferFinished;
    }
    else if (ConditionCode == 6) {
        return TransferStalled;
    }
    else if (ConditionCode == 1) {
        return TransferInvalidToggles;
    }
    else if (ConditionCode == 2) {
        return TransferNotResponding;
    }
    else if (ConditionCode == 3) {
        return TransferNAK;
    }
    else if (ConditionCode == 4) {
        return TransferBabble;
    }
    else if (ConditionCode == 5) {
        return TransferBufferError;
    }
    else {
        TRACE("Error: 0x%x (%s)", ConditionCode, UhciErrorMessages[ConditionCode]);
        return TransferInvalid;
    }
}

OsStatus_t
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

    // Debug
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
        int Index = UhciDetermineInterruptIndex((size_t)i);
        UsbSchedulerGetPoolElement(Controller->Base.Scheduler, UHCI_QH_POOL, 
            Index, (uint8_t**)&Qh, &AsyncQhPhysical);

        Controller->Base.Scheduler->VirtualFrameList[i]   = (uintptr_t)Qh;
        Controller->Base.Scheduler->Settings.FrameList[i] = AsyncQhPhysical | UHCI_LINK_QH;
    }
    return OsSuccess;
}

OsStatus_t
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

OsStatus_t
UhciQueueReset(
    _In_ UhciController_t* Controller)
{
    TRACE("UhciQueueReset()");

    // Stop Controller
    if (UhciStop(Controller) != OsSuccess) {
        ERROR("Failed to stop the controller");
        return OsError;
    }
    UsbManagerClearTransfers(&Controller->Base);
    return UhciQueueResetInternalData(Controller);
}

OsStatus_t
UhciQueueDestroy(
    _In_ UhciController_t* Controller)
{
    TRACE("UhciQueueDestroy()");

    // Make sure everything is unscheduled, reset and clean
    UhciQueueReset(Controller);
    UsbSchedulerDestroy(Controller->Base.Scheduler);
    return OsSuccess;
}

// This should be called regularly to keep the stored frame relevant
void
UhciUpdateCurrentFrame(
    _In_ UhciController_t* Controller)
{
    uint16_t FrameNo = 0;
    int      Delta   = 0;

    // Read the current frame, and use the last read frame to calculate the delta
    // then add to current frame
    FrameNo             = UhciRead16(Controller, UHCI_REGISTER_FRNUM);
    Delta               = (FrameNo - Controller->Frame) & UHCI_FRAME_MASK;
    Controller->Frame   += Delta;
}

int
UhciConditionCodeToIndex(
    _In_ int ConditionCode)
{
    int bCount = 0;
    int Cc     = ConditionCode;

    // Keep bit-shifting and count which bit is set
    for (; Cc != 0;) {
        bCount++;
        Cc >>= 1;
    }
    if (bCount >= 8) {
        bCount = 0;
    }
    return bCount;
}

int
HciProcessElement(
    _In_ UsbManagerController_t* Controller,
    _In_ uint8_t*                Element,
    _In_ int                     Reason,
    _In_ void*                   Context)
{
    UhciTransferDescriptor_t* Td       = (UhciTransferDescriptor_t*)Element;
    UsbManagerTransfer_t*     Transfer = (UsbManagerTransfer_t*)Context;

    // Debug
    TRACE("UhciProcessElement(Reason %i)", Reason);
    switch (Reason) {
        case USB_REASON_DUMP: {
            if (Transfer->Transfer.Type != IsochronousTransfer
                && Element == (uint8_t*)Transfer->EndpointDescriptor) {
                UhciQhDump((UhciController_t*)Controller, (UhciQueueHead_t*)Td);
            }
            else {
                UhciTdDump((UhciController_t*)Controller, Td);
            }
        } break;
        
        case USB_REASON_SCAN: {
            // If we have a queue-head allocated skip it
            if (Transfer->Transfer.Type != IsochronousTransfer && 
                Element == (uint8_t*)Transfer->EndpointDescriptor) {
                // Skip scan on queue-heads
                return ITERATOR_CONTINUE;
            }

            // Perform validation and handle short transfers
            UhciTdValidate(Transfer, Td);
            if (Transfer->Flags & TransferFlagShort) {
                return ITERATOR_STOP; // Stop here
            }
        } break;
        
        case USB_REASON_RESET: {
            if (Transfer->Transfer.Type != IsochronousTransfer) {
                if (Element != (uint8_t*)Transfer->EndpointDescriptor) {
                    UhciTdRestart(Transfer, Td);
                }
            }
            else {
                UhciTdRestart(Transfer, Td);
            }
        } break;
        
        case USB_REASON_FIXTOGGLE: {
            // If we have a queue-head allocated skip it
            if (Transfer->Transfer.Type != IsochronousTransfer
                && Element == (uint8_t*)Transfer->EndpointDescriptor) {
                // Skip sync on queue-heads
                return ITERATOR_CONTINUE;
            }
            UhciTdSynchronize(Transfer, Td);
        } break;

        case USB_REASON_LINK: {
            // If it's a queue head link that
            if (Transfer->Transfer.Type != IsochronousTransfer) {
                UhciQhLink((UhciController_t*)Controller, (UhciQueueHead_t*)Element);
                return ITERATOR_STOP;
            }
            else {
                // Link all elements
                UsbSchedulerLinkPeriodicElement(Controller->Scheduler, UHCI_TD_POOL, Element);
            }
        } break;
        
        case USB_REASON_UNLINK: {
            // If it's a queue head link that
            if (Transfer->Transfer.Type != IsochronousTransfer) {
                UhciQhUnlink((UhciController_t*)Controller, (UhciQueueHead_t*)Element);
                return ITERATOR_STOP;
            }
            else {
                // Link all elements
                UsbSchedulerUnlinkPeriodicElement(Controller->Scheduler, UHCI_TD_POOL, Element);
            }
        } break;
        
        case USB_REASON_CLEANUP: {
            // Very simple cleanup
            UsbSchedulerFreeElement(Controller->Scheduler, Element);
        } break;
    }
    return ITERATOR_CONTINUE;
}

void
HciProcessEvent(
    _In_ UsbManagerController_t* Controller,
    _In_ int                     Event,
    _In_ void*                   Context)
{
    UsbManagerTransfer_t* Transfer = (UsbManagerTransfer_t*)Context;
    TRACE("UhciProcessEvent(Event %i)", Event);

    switch (Event) {
        case USB_EVENT_RESTART_DONE: {
            if (Transfer->Transfer.Type != IsochronousTransfer) {
                UhciQhRestart((UhciController_t*)Controller, Transfer);
            }
        } break;
    }
}

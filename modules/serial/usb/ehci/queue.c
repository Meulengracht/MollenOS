/* MollenOS
 *
 * Copyright 2018, Philip Meulengracht
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
 * MollenOS MCore - Enhanced Host Controller Interface Driver
 * TODO:
 * - Power Management
 * - FSTN Transport
 * - Split-Isochronous Transport
 */
//#define __TRACE

/* Includes
 * - System */
#include <os/mollenos.h>
#include <ddk/utils.h>
#include "ehci.h"

/* Includes
 * - Library */
#include <stdlib.h>
#include <assert.h>
#include <string.h>

/* Globals 
 * Error messages for codes that might appear in transfers */
const char *EhciErrorMessages[] = {
    "No Error",
    "Ping State/PERR",
    "Split Transaction State",
    "Missed Micro-Frame",
    "Transaction Error (CRC, Timeout)",
    "Babble Detected",
    "Data Buffer Error",
    "Halted, Stall",
    "Active"
};

/* EhciQueueResetInternalData
 * Removes and cleans up any existing transfers, then reinitializes. */
OsStatus_t
EhciQueueResetInternalData(
    _In_ EhciController_t*  Controller)
{
    // Variables
    EhciTransferDescriptor_t *NullTd    = NULL;
    EhciQueueHead_t *AsyncQh            = NULL;
    uintptr_t AsyncQhPhysical           = 0;
    uintptr_t NullTdPhysical            = 0;

    // Reset all tds and qhs
    UsbSchedulerResetInternalData(Controller->Base.Scheduler, 1, 1);
    UsbSchedulerGetPoolElement(Controller->Base.Scheduler, EHCI_QH_POOL, 
        EHCI_QH_ASYNC, (uint8_t**)&AsyncQh, &AsyncQhPhysical);
    UsbSchedulerGetPoolElement(Controller->Base.Scheduler, EHCI_TD_POOL, 
        EHCI_TD_NULL, (uint8_t**)&NullTd, &NullTdPhysical);
    
    // Initialize the dummy (async) queue-head that we use for end-link
    // It must be a circular queue, so must always point back to 
    AsyncQh->Flags                      = EHCI_QH_RECLAMATIONHEAD | EHCI_QH_HIGHSPEED | 
        EHCI_QH_EPADDR(1) | EHCI_QH_MAXLENGTH(64);
    AsyncQh->Object.Flags               |= EHCI_LINK_QH;

    AsyncQh->LinkPointer                = AsyncQhPhysical | EHCI_LINK_QH;
    AsyncQh->Object.BreathIndex         = AsyncQh->Object.Index;

    AsyncQh->Overlay.NextTD             = NullTdPhysical;
    AsyncQh->Object.DepthIndex          = NullTd->Object.Index;

    AsyncQh->Overlay.NextAlternativeTD  = EHCI_LINK_END;
    AsyncQh->Overlay.Status             = EHCI_TD_HALTED;

    // Initialize the dummy (async) transfer-descriptor that we use for queuing short-transfers
    // to make sure that short transfers cancel
    NullTd->Token                       = EHCI_TD_OUT;
    NullTd->Status                      = EHCI_TD_HALTED;
    NullTd->Link                        = EHCI_LINK_END;
    NullTd->AlternativeLink             = EHCI_LINK_END;
    return OsSuccess;
}

/* EhciQueueInitialize
 * Initialize the controller's queue resources and resets counters */
OsStatus_t
EhciQueueInitialize(
    _In_ EhciController_t*  Controller)
{
    // Variables
    UsbSchedulerSettings_t Settings;
    Flags_t SchedulerFlags = 0;

    // Trace
    TRACE("EhciQueueInitialize()");

    // Select a queue size
    if (Controller->CParameters & (EHCI_CPARAM_VARIABLEFRAMELIST | EHCI_CPARAM_32FRAME_SUPPORT)) {
        if (Controller->CParameters & EHCI_CPARAM_32FRAME_SUPPORT) {
            Controller->FrameCount  = 32;
        }
        else {
            Controller->FrameCount  = 256;
        }
    }
    else {
        Controller->FrameCount      = 1024;
    }

    // Initialize the scheduler
    TRACE(" > Configuring scheduler");
    SchedulerFlags = USB_SCHEDULER_DEFERRED_CLEAN | USB_SCHEDULER_FRAMELIST | USB_SCHEDULER_LINK_BIT_EOL;
    if (Controller->CParameters & EHCI_CPARAM_64BIT) {
#ifdef __OSCONFIG_EHCI_ALLOW_64BIT
        SchedulerFlags |= USB_SCHEDULER_FL64;
#endif
    }
    UsbSchedulerSettingsCreate(&Settings, Controller->FrameCount, 8, EHCI_MAX_BANDWIDTH, SchedulerFlags);

    // Add Queue-Heads
    UsbSchedulerSettingsAddPool(&Settings, sizeof(EhciQueueHead_t), EHCI_QH_ALIGNMENT, EHCI_QH_COUNT, 
        EHCI_QH_START, offsetof(EhciQueueHead_t, LinkPointer), 
        offsetof(EhciQueueHead_t, Overlay.NextTD), offsetof(EhciQueueHead_t, Object));
    
    // Add transfer-descriptors
    // Alternative Link is Breath
    // Link is Depth
    UsbSchedulerSettingsAddPool(&Settings, sizeof(EhciTransferDescriptor_t), EHCI_TD_ALIGNMENT, EHCI_TD_COUNT, 
        EHCI_TD_START, offsetof(EhciTransferDescriptor_t, AlternativeLink), 
        offsetof(EhciTransferDescriptor_t, Link), offsetof(EhciTransferDescriptor_t, Object));
    
    // Add isochronous transfer descriptors
    UsbSchedulerSettingsAddPool(&Settings, sizeof(EhciIsochronousDescriptor_t), EHCI_iTD_ALIGNMENT, EHCI_iTD_COUNT, 
        EHCI_iTD_START, offsetof(EhciIsochronousDescriptor_t, Link), 
        offsetof(EhciIsochronousDescriptor_t, Link), offsetof(EhciIsochronousDescriptor_t, Object));
    
    // Add split-isochronous transfer descriptors
    UsbSchedulerSettingsAddPool(&Settings, sizeof(EhciSplitIsochronousDescriptor_t), EHCI_siTD_ALIGNMENT, EHCI_siTD_COUNT, 
        EHCI_siTD_START, offsetof(EhciSplitIsochronousDescriptor_t, Link), 
        offsetof(EhciSplitIsochronousDescriptor_t, Link), offsetof(EhciSplitIsochronousDescriptor_t, Object));
    
    // Create the scheduler
    TRACE(" > Initializing scheduler");
    UsbSchedulerInitialize(&Settings, &Controller->Base.Scheduler);
    return EhciQueueResetInternalData(Controller);
}

/* EhciQueueReset
 * Removes and cleans up any existing transfers, then reinitializes. */
OsStatus_t
EhciQueueReset(
    _In_ EhciController_t*  Controller)
{
    // Debug
    TRACE("EhciQueueReset()");

    // Stop Controller
    EhciHalt(Controller);
    UsbManagerClearTransfers(&Controller->Base);
    return EhciQueueResetInternalData(Controller);
}

/* EhciQueueDestroy
 * Unschedules any scheduled ed's and frees all resources allocated
 * by the initialize function */
OsStatus_t
EhciQueueDestroy(
    _In_ EhciController_t*  Controller)
{
    // Debug
    TRACE("EhciQueueDestroy()");

    // Reset first
    EhciQueueReset(Controller);
    return UsbSchedulerDestroy(Controller->Base.Scheduler);
}

/* EhciConditionCodeToIndex
 * Converts a given condition bit-index to number */
int
EhciConditionCodeToIndex(
    _In_ unsigned           ConditionCode)
{
    // Variables
    unsigned Cc = ConditionCode;
    int bCount  = 0;

    // Shift untill we reach 0, count number of shifts
    for (; Cc != 0;) {
        bCount++;
        Cc >>= 1;
    }
    return bCount;
}

/* EhciGetStatusCode
 * Retrieves a status-code from a given condition code */
UsbTransferStatus_t
EhciGetStatusCode(
    _In_ int                ConditionCode)
{
    // One huuuge if/else
    if (ConditionCode == 0) {
        return TransferFinished;
    }
    else if (ConditionCode == 4) {
        return TransferNotResponding;
    }
    else if (ConditionCode == 5) {
        return TransferBabble;
    }
    else if (ConditionCode == 6) {
        return TransferBufferError;
    }
    else if (ConditionCode == 7) {
        return TransferStalled;
    }
    else {
        WARNING("EHCI-Error: 0x%x (%s)", ConditionCode, EhciErrorMessages[ConditionCode]);
        return TransferInvalid;
    }
}

/* EhciSetPrefetching
 * Disables the prefetching related to the transfer-type. */
OsStatus_t
EhciSetPrefetching(
    _In_ EhciController_t*  Controller,
    _In_ UsbTransferType_t  Type,
    _In_ int                Set)
{
    // Variables
    reg32_t Command         = Controller->OpRegisters->UsbCommand;
    if (!(Controller->CParameters & EHCI_CPARAM_HWPREFETCH)) {
        return OsError;
    }
    
    // Detect type of prefetching
    if (Type == ControlTransfer || Type == BulkTransfer) {
        if (!Set) {
            Command &= ~(EHCI_COMMAND_ASYNC_PREFETCH);
            Controller->OpRegisters->UsbCommand = Command;
            MemoryBarrier();
            while (Controller->OpRegisters->UsbCommand & EHCI_COMMAND_ASYNC_PREFETCH);
        }
        else {
            Command |= EHCI_COMMAND_ASYNC_PREFETCH;
            Controller->OpRegisters->UsbCommand = Command;
        }
    }
    else {
        if (!Set) {
            Command &= ~(EHCI_COMMAND_PERIOD_PREFECTCH);
            Controller->OpRegisters->UsbCommand = Command;
            MemoryBarrier();
            while (Controller->OpRegisters->UsbCommand & EHCI_COMMAND_PERIOD_PREFECTCH);
        }
        else {
            Command |= EHCI_COMMAND_PERIOD_PREFECTCH;
            Controller->OpRegisters->UsbCommand = Command;
        }
    }
    return OsSuccess;
}

/* EhciEnableScheduler
 * Enables the relevant scheduler if it is not enabled already */
void
EhciEnableScheduler(
    _In_ EhciController_t*  Controller,
    _In_ UsbTransferType_t  Type)
{
    // Variables
    reg32_t Temp    = 0;

    // Sanitize the current status
    if (Type == ControlTransfer || Type == BulkTransfer) {
        if (Controller->OpRegisters->UsbStatus & EHCI_STATUS_ASYNC_ACTIVE) {
            // Should we ring the doorbell? I don't believe it's entirely neccessary
            // as we use reclamation heads @todo
            return;
        }

        // Fire the enable command
        Temp                                = Controller->OpRegisters->UsbCommand;
        Temp                                |= EHCI_COMMAND_ASYNC_ENABLE;
        Controller->OpRegisters->UsbCommand = Temp;
    }
    else {
        if (Controller->OpRegisters->UsbStatus & EHCI_STATUS_PERIODIC_ACTIVE) {
            return;
        }

        // Fire the enable command
        Temp                                = Controller->OpRegisters->UsbCommand;
        Temp                                |= EHCI_COMMAND_PERIODIC_ENABLE;
        Controller->OpRegisters->UsbCommand = Temp;
    }
}

/* EhciDisableScheduler
 * Disables the sheduler if it is not disabled already */
void
EhciDisableScheduler(
    _In_ EhciController_t*  Controller,
    _In_ UsbTransferType_t  Type)
{
    // Variables
    reg32_t Temp    = 0;

    // Sanitize its current status
    if (Type == ControlTransfer || Type == BulkTransfer) {
        if (!(Controller->OpRegisters->UsbStatus & EHCI_STATUS_ASYNC_ACTIVE)) {
            return;
        }

        // Fire off disable command
        Temp                                = Controller->OpRegisters->UsbCommand;
        Temp                                &= ~(EHCI_COMMAND_ASYNC_ENABLE);
        Controller->OpRegisters->UsbCommand = Temp;
    }
    else {
        if (!(Controller->OpRegisters->UsbStatus & EHCI_STATUS_PERIODIC_ACTIVE)) {
            return;
        }

        // Fire off disable command
        Temp                                = Controller->OpRegisters->UsbCommand;
        Temp                                &= ~(EHCI_COMMAND_PERIODIC_ENABLE);
        Controller->OpRegisters->UsbCommand = Temp;
    }
}

/* EhciRingDoorbell
 * This functions rings the bell */
void
EhciRingDoorbell(
    _In_ EhciController_t*  Controller)
{
    // Do not ring the doorbell if the schedule is not running
    if (Controller->OpRegisters->UsbStatus & EHCI_STATUS_ASYNC_ACTIVE) {
        Controller->OpRegisters->UsbCommand |= EHCI_COMMAND_IOC_ASYNC_DOORBELL;
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
    // Variables
    UsbManagerTransfer_t *Transfer  = (UsbManagerTransfer_t*)Context;
    UsbSchedulerPool_t *QhPool      = &Controller->Scheduler->Settings.Pools[EHCI_QH_POOL];
    UsbSchedulerPool_t *Pool        = NULL;
    uint8_t *AsyncRootElement       = NULL;
    UsbSchedulerGetPoolFromElement(Controller->Scheduler, Element, &Pool);
    assert(Pool != NULL);

    // Debug
    TRACE("EhciProcessElement(Reason %i)", Reason);
    
    // Handle the reasons
    switch (Reason) {
        case USB_REASON_DUMP: {
            if (Transfer->Transfer.Type != IsochronousTransfer) {
                if (Pool == QhPool) {
                    EhciQhDump((EhciController_t*)Controller, (EhciQueueHead_t*)Element);
                }
                else {
                    EhciTdDump((EhciController_t*)Controller, (EhciTransferDescriptor_t*)Element);
                }
            }
            else {
                EhciiTdDump((EhciController_t*)Controller, (EhciIsochronousDescriptor_t*)Element);
            }
        } break;
        
        case USB_REASON_SCAN: {
            if (Pool == QhPool) {
                return ITERATOR_CONTINUE; // Skip scan on queue-heads
            }

            if (Transfer->Transfer.Type != IsochronousTransfer) {
                EhciTdValidate(Transfer, (EhciTransferDescriptor_t*)Element);
                if (Transfer->Flags & TransferFlagShort) {
                    return ITERATOR_STOP; // Stop here
                }
            }
            else {
                EhciiTdValidate(Transfer, (EhciIsochronousDescriptor_t*)Element);
            }
        } break;
        
        case USB_REASON_RESET: {
            if (Pool != QhPool) {
                if (Transfer->Transfer.Type != IsochronousTransfer) {
                    EhciTdRestart((EhciController_t*)Controller, Transfer, (EhciTransferDescriptor_t*)Element);
                }
                else {
                    EhciiTdRestart((EhciController_t*)Controller, Transfer, (EhciIsochronousDescriptor_t*)Element);
                }
            }
        } break;
        
        case USB_REASON_FIXTOGGLE: {
            // Isochronous transfers don't use toggles
            if (Transfer->Transfer.Type != IsochronousTransfer) {
                if (Pool == QhPool) {
                    return ITERATOR_CONTINUE; // Skip sync on queue-heads
                }
                EhciTdSynchronize(Transfer, (EhciTransferDescriptor_t*)Element);
            }
        } break;

        case USB_REASON_LINK: {
            // If it's a queue head link that
            if (Pool == QhPool) {
                SpinlockAcquire(&Controller->Lock);
                EhciSetPrefetching((EhciController_t*)Controller, Transfer->Transfer.Type, 0);
                if (Transfer->Transfer.Type == ControlTransfer || Transfer->Transfer.Type == BulkTransfer) {
                    UsbSchedulerGetPoolElement(Controller->Scheduler, EHCI_QH_POOL, EHCI_QH_ASYNC, &AsyncRootElement, NULL);
                    UsbSchedulerChainElement(Controller->Scheduler, AsyncRootElement, Element, USB_ELEMENT_NO_INDEX, USB_CHAIN_BREATH);
                }
                else {
                    UsbSchedulerLinkPeriodicElement(Controller->Scheduler, Element);
                }
                EhciSetPrefetching((EhciController_t*)Controller, Transfer->Transfer.Type, 1);
                EhciEnableScheduler((EhciController_t*)Controller, Transfer->Transfer.Type);
                SpinlockRelease(&Controller->Lock);
                return ITERATOR_STOP;
            }
        } break;
        
        case USB_REASON_UNLINK: {
            // If it's a queue head link that
            if (Pool == QhPool) {
                SpinlockAcquire(&Controller->Lock);
                EhciSetPrefetching((EhciController_t*)Controller, Transfer->Transfer.Type, 0);
                if (Transfer->Transfer.Type == ControlTransfer || Transfer->Transfer.Type == BulkTransfer) {
                    UsbSchedulerGetPoolElement(Controller->Scheduler, EHCI_QH_POOL, EHCI_QH_ASYNC, &AsyncRootElement, NULL);
                    UsbSchedulerUnchainElement(Controller->Scheduler, AsyncRootElement, Element, USB_CHAIN_BREATH);
                }
                else {
                    UsbSchedulerUnlinkPeriodicElement(Controller->Scheduler, Element);
                }
                EhciSetPrefetching((EhciController_t*)Controller, Transfer->Transfer.Type, 1);
                SpinlockRelease(&Controller->Lock);
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
    TRACE("EhciProcessEvent(Event %i)", Event);

    // Handle the reasons
    switch (Event) {
        case USB_EVENT_RESTART_DONE: {
            if (Transfer->Transfer.Type != IsochronousTransfer) {
                EhciQhRestart((EhciController_t*)Controller, Transfer);
            }
        } break;
    }
}

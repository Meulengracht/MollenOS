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
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * MollenOS MCore - Enhanced Host Controller Interface Driver
 * TODO:
 * - Power Management
 * - FSTN Transport
 * - Split-Isochronous Transport
 */
//#define __TRACE
#define __need_minmax
#include <os/mollenos.h>
#include <ddk/utils.h>
#include "ehci.h"

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
oserr_t
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
    return OS_EOK;
}

/* EhciQueueInitialize
 * Initialize the controller's queue resources and resets counters */
oserr_t
EhciQueueInitialize(
    _In_ EhciController_t*  Controller)
{
    // Variables
    UsbSchedulerSettings_t Settings;
    unsigned int SchedulerFlags = 0;

    // Trace
    TRACE("EhciQueueInitialize()");

    // Select a queue size
    if (Controller->CParameters & (EHCI_CPARAM_VARIABLEFRAMELIST | EHCI_CPARAM_32FRAME_SUPPORT)) {
        if (Controller->CParameters & EHCI_CPARAM_32FRAME_SUPPORT) {
            Controller->FrameCount = 32;
        } else {
            Controller->FrameCount = 256;
        }
    } else {
        Controller->FrameCount = 1024;
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
oserr_t
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

oserr_t
EhciQueueDestroy(
    _In_ EhciController_t* Controller)
{
    // Debug
    TRACE("EhciQueueDestroy()");

    // Reset first
    EhciQueueReset(Controller);
    UsbSchedulerDestroy(Controller->Base.Scheduler);
    return OS_EOK;
}

int
EHCIConditionCodeToIndex(
    _In_ unsigned ConditionCode)
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

enum USBTransferCode
EHCIErrorCodeToTransferStatus(
    _In_ int ConditionCode)
{
    // One huuuge if/else
    if (ConditionCode == 0) {
        return USBTRANSFERCODE_SUCCESS;
    } else if (ConditionCode == 4) {
        return USBTRANSFERCODE_NORESPONSE;
    } else if (ConditionCode == 5) {
        return USBTRANSFERCODE_BABBLE;
    } else if (ConditionCode == 6) {
        return USBTRANSFERCODE_BUFFERERROR;
    } else if (ConditionCode == 7) {
        return USBTRANSFERCODE_STALL;
    } else {
        WARNING("EHCI-Error: 0x%x (%s)", ConditionCode, EhciErrorMessages[ConditionCode]);
        return USBTRANSFERCODE_INVALID;
    }
}

oserr_t
EhciSetPrefetching(
    _In_ EhciController_t*  Controller,
    _In_ uint8_t  Type,
    _In_ int                Set)
{
    reg32_t Command = READ_VOLATILE(Controller->OpRegisters->UsbCommand);
    if (!(Controller->CParameters & EHCI_CPARAM_HWPREFETCH)) {
        return OS_EUNKNOWN;
    }
    
    // Detect type of prefetching
    if (Type == USBTRANSFER_TYPE_CONTROL || Type == USBTRANSFER_TYPE_BULK) {
        if (!Set) {
            Command &= ~(EHCI_COMMAND_ASYNC_PREFETCH);
            WRITE_VOLATILE(Controller->OpRegisters->UsbCommand, Command);
            while (READ_VOLATILE(Controller->OpRegisters->UsbCommand) & EHCI_COMMAND_ASYNC_PREFETCH);
        }
        else {
            Command |= EHCI_COMMAND_ASYNC_PREFETCH;
            WRITE_VOLATILE(Controller->OpRegisters->UsbCommand, Command);
        }
    }
    else {
        if (!Set) {
            Command &= ~(EHCI_COMMAND_PERIOD_PREFECTCH);
            WRITE_VOLATILE(Controller->OpRegisters->UsbCommand, Command);
            while (READ_VOLATILE(Controller->OpRegisters->UsbCommand) & EHCI_COMMAND_PERIOD_PREFECTCH);
        }
        else {
            Command |= EHCI_COMMAND_PERIOD_PREFECTCH;
            WRITE_VOLATILE(Controller->OpRegisters->UsbCommand, Command);
        }
    }
    return OS_EOK;
}

void
EhciEnableScheduler(
    _In_ EhciController_t*  Controller,
    _In_ uint8_t  Type)
{
    reg32_t Status  = READ_VOLATILE(Controller->OpRegisters->UsbStatus);
    reg32_t Command = READ_VOLATILE(Controller->OpRegisters->UsbCommand);

    // Sanitize the current status
    if (Type == USBTRANSFER_TYPE_CONTROL || Type == USBTRANSFER_TYPE_BULK) {
        if (Status & EHCI_STATUS_ASYNC_ACTIVE) {
            // Should we ring the doorbell? I don't believe it's entirely neccessary
            // as we use reclamation heads @todo
            return;
        }
        Command |= EHCI_COMMAND_ASYNC_ENABLE;
        WRITE_VOLATILE(Controller->OpRegisters->UsbCommand, Command);
    } else {
        if (Status & EHCI_STATUS_PERIODIC_ACTIVE) {
            return;
        }
        Command |= EHCI_COMMAND_PERIODIC_ENABLE;
        WRITE_VOLATILE(Controller->OpRegisters->UsbCommand, Command);
    }
}

void
EhciDisableScheduler(
    _In_ EhciController_t*  Controller,
    _In_ uint8_t  Type)
{
    reg32_t Status  = READ_VOLATILE(Controller->OpRegisters->UsbStatus);
    reg32_t Command = READ_VOLATILE(Controller->OpRegisters->UsbCommand);

    // Sanitize its current status
    if (Type == USBTRANSFER_TYPE_CONTROL || Type == USBTRANSFER_TYPE_BULK) {
        if (!(Status & EHCI_STATUS_ASYNC_ACTIVE)) {
            return;
        }
        Command &= ~(EHCI_COMMAND_ASYNC_ENABLE);
        WRITE_VOLATILE(Controller->OpRegisters->UsbCommand, Command);
    } else {
        if (!(Status & EHCI_STATUS_PERIODIC_ACTIVE)) {
            return;
        }
        Command &= ~(EHCI_COMMAND_PERIODIC_ENABLE);
        WRITE_VOLATILE(Controller->OpRegisters->UsbCommand, Command);
    }
}

void
EhciRingDoorbell(
    _In_ EhciController_t*  Controller)
{
    // Do not ring the doorbell if the schedule is not running
    if (READ_VOLATILE(Controller->OpRegisters->UsbStatus) & EHCI_STATUS_ASYNC_ACTIVE) {
        reg32_t Command = READ_VOLATILE(Controller->OpRegisters->UsbCommand);
        WRITE_VOLATILE(Controller->OpRegisters->UsbCommand, 
            Command | EHCI_COMMAND_IOC_ASYNC_DOORBELL);
    }
}

bool
HCIProcessElement(
        _In_ UsbManagerController_t* controller,
        _In_ uint8_t*                element,
        _In_ enum HCIProcessReason   reason,
        _In_ void*                   context)
{
    UsbSchedulerPool_t* qhPool = &controller->Scheduler->Settings.Pools[EHCI_QH_POOL];
    UsbSchedulerPool_t* pool  = NULL;
    uint8_t* asyncRootElement = NULL;

    UsbSchedulerGetPoolFromElement(controller->Scheduler, element, &pool);
    assert(pool != NULL);
    TRACE("EhciProcessElement(reason=%i)", reason);
    
    switch (reason) {
        case HCIPROCESS_REASON_DUMP: {
            UsbManagerTransfer_t* transfer = context;
            if (transfer->Type != USBTRANSFER_TYPE_ISOC) {
                if (pool == qhPool) {
                    EHCIQHDump((EhciController_t*)controller, (EhciQueueHead_t*)element);
                } else {
                    EHCITDDump((EhciController_t*)controller, (EhciTransferDescriptor_t*)element);
                }
            } else {
                EHCIITDDump((EhciController_t*)controller, (EhciIsochronousDescriptor_t*)element);
            }
        } break;
        
        case HCIPROCESS_REASON_SCAN: {
            struct HCIProcessReasonScanContext* scanContext = context;

            if (pool == qhPool) {
                return true; // Skip scan on queue-heads
            }

            if (scanContext->Transfer->Type != USBTRANSFER_TYPE_ISOC) {
                EHCITDVerify(scanContext, (EhciTransferDescriptor_t*)element);
            } else {
                EHCIITDVerify(scanContext, (EhciIsochronousDescriptor_t*)element);
            }
        } break;
        
        case HCIPROCESS_REASON_RESET: {
            UsbManagerTransfer_t* transfer = context;
            if (pool != qhPool) {
                if (transfer->Type != USBTRANSFER_TYPE_ISOC) {
                    EHCITDRestart((EhciController_t*)controller, transfer, (EhciTransferDescriptor_t*)element);
                } else {
                    EHCIITDRestart((EhciIsochronousDescriptor_t*)element);
                }
            }
        } break;

        case HCIPROCESS_REASON_LINK: {
            UsbManagerTransfer_t* transfer = context;
            // If it's a queue head link that
            if (pool == qhPool) {
                spinlock_acquire(&controller->Lock);
                EhciSetPrefetching((EhciController_t*)controller, transfer->Type, 0);
                if (__Transfer_IsAsync(transfer)) {
                    UsbSchedulerGetPoolElement(
                            controller->Scheduler,
                            EHCI_QH_POOL,
                            EHCI_QH_ASYNC,
                            &asyncRootElement,
                            NULL
                    );
                    UsbSchedulerChainElement(
                            controller->Scheduler,
                            EHCI_QH_POOL,
                            asyncRootElement,
                            EHCI_QH_POOL, element,
                            USB_ELEMENT_NO_INDEX,
                            USB_CHAIN_BREATH
                    );
                } else {
                    UsbSchedulerLinkPeriodicElement(
                            controller->Scheduler,
                            EHCI_QH_POOL,
                            element
                    );
                }
                EhciSetPrefetching((EhciController_t*)controller, transfer->Type, 1);
                EhciEnableScheduler((EhciController_t*)controller, transfer->Type);
                spinlock_release(&controller->Lock);
                return false;
            }
        } break;
        
        case HCIPROCESS_REASON_UNLINK: {
            UsbManagerTransfer_t* transfer = context;
            // If it's a queue head link that
            if (pool == qhPool) {
                spinlock_acquire(&controller->Lock);
                EhciSetPrefetching((EhciController_t*)controller, transfer->Type, 0);
                if (__Transfer_IsAsync(transfer)) {
                    UsbSchedulerGetPoolElement(controller->Scheduler, EHCI_QH_POOL, EHCI_QH_ASYNC, &asyncRootElement, NULL);
                    UsbSchedulerUnchainElement(controller->Scheduler, EHCI_QH_POOL, asyncRootElement, EHCI_QH_POOL, element, USB_CHAIN_BREATH);
                } else {
                    UsbSchedulerUnlinkPeriodicElement(controller->Scheduler, EHCI_QH_POOL, element);
                }
                EhciSetPrefetching((EhciController_t*)controller, transfer->Type, 1);
                spinlock_release(&controller->Lock);
                return false;
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
    UsbManagerTransfer_t* transfer = context;
    TRACE("EHCIProcessEvent(event=%i)", event);

    switch (event) {
        case HCIPROCESS_EVENT_RESET_DONE: {
            if (transfer->Type != USBTRANSFER_TYPE_ISOC) {
                EHCIQHRestart((EhciController_t*)controller, transfer);
            }
        } break;
    }
}

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

#ifndef __USB_XHCI__
#define __USB_XHCI__

#include <os/osdefs.h>
#include <os/types/handle.h>
#include <os/types/shm.h>

#include "../common/hci.h"
#include "../common/manager.h"

/**
 * Generic xHCI constants. xHCI uses USB-defined MMIO register blocks like EHCI,
 * but all scheduling state is driven by rings and device contexts rather than
 * the periodic/asynchronous frame lists used by the earlier HCIs.
 */
#define XHCI_COMMAND_RING_ENTRIES   256
#define XHCI_EVENT_RING_ENTRIES     256
#define XHCI_ERST_ENTRIES           1
#define XHCI_MAX_DEVICE_CONTEXTS    256

#define XHCI_TRB_ALIGNMENT          16
#define XHCI_CONTEXT_ALIGNMENT      64
#define XHCI_PAGE_SIZE              0x1000

PACKED_ATYPESTRUCT(volatile, XhciCapabilityRegisters, {
    uint8_t  CapLength;
    uint8_t  Reserved;
    uint16_t HciVersion;
    reg32_t  HcsParams1;
    reg32_t  HcsParams2;
    reg32_t  HcsParams3;
    reg32_t  HccParams1;
    reg32_t  DoorbellOffset;
    reg32_t  RuntimeOffset;
    reg32_t  HccParams2;
});

#define XHCI_HCSPARAMS1_MAXSLOTS(n)      ((n) & 0xFF)
#define XHCI_HCSPARAMS1_MAXINTRS(n)      (((n) >> 8) & 0x7FF)
#define XHCI_HCSPARAMS1_MAXPORTS(n)      (((n) >> 24) & 0xFF)
#define XHCI_HCSPARAMS2_MAXSCRATCHPADS(n) ((((n) >> 27) & 0x1F) | (((n) >> 16) & 0x3E0))
#define XHCI_HCCPARAMS1_AC64             (1 << 0)
#define XHCI_HCCPARAMS1_CSZ              (1 << 2)

PACKED_ATYPESTRUCT(volatile, XhciOperationalRegisters, {
    reg32_t UsbCommand;
    reg32_t UsbStatus;
    reg32_t PageSize;
    reg32_t Reserved0[2];
    reg32_t DeviceNotificationControl;
    reg64_t CommandRingControl;
    reg32_t Reserved1[4];
    reg64_t DeviceContextBaseAddressArray;
    reg32_t Configure;
});

#define XHCI_OP_USBCMD_RUN               (1 << 0)
#define XHCI_OP_USBCMD_HCRESET           (1 << 1)
#define XHCI_OP_USBCMD_INTERRUPTER       (1 << 2)
#define XHCI_OP_USBSTS_HALTED            (1 << 0)
#define XHCI_OP_USBSTS_HOSTERROR         (1 << 2)
#define XHCI_OP_USBSTS_EVENT             (1 << 3)
#define XHCI_OP_USBSTS_NOT_READY         (1 << 11)
#define XHCI_OP_CONFIG_MAXSLOTS(n)       ((n) & 0xFF)
#define XHCI_OP_CRCR_RING_CYCLE          (1 << 0)

PACKED_ATYPESTRUCT(volatile, XhciPortRegisters, {
    reg32_t StatusControl;
    reg32_t PowerManagementStatusControl;
    reg32_t LinkInfo;
    reg32_t HardwareLpmControl;
});

#define XHCI_PORT_STATUS_CONNECTED       (1 << 0)
#define XHCI_PORT_STATUS_ENABLED         (1 << 1)
#define XHCI_PORT_STATUS_RESET           (1 << 4)
#define XHCI_PORT_STATUS_POWER           (1 << 9)
#define XHCI_PORT_STATUS_SPEED(n)        (((n) >> 10) & 0xF)
#define XHCI_PORT_STATUS_CONNECT_CHANGE  (1 << 17)
#define XHCI_PORT_STATUS_ENABLE_CHANGE   (1 << 18)
#define XHCI_PORT_STATUS_RESET_CHANGE    (1 << 21)
#define XHCI_PORT_STATUS_LINK_CHANGE     (1 << 22)
#define XHCI_PORT_STATUS_CONFIG_ERROR    (1 << 23)
#define XHCI_PORT_STATUS_CHANGE_BITS     (XHCI_PORT_STATUS_CONNECT_CHANGE | \
                                          XHCI_PORT_STATUS_ENABLE_CHANGE | \
                                          XHCI_PORT_STATUS_RESET_CHANGE | \
                                          XHCI_PORT_STATUS_LINK_CHANGE | \
                                          XHCI_PORT_STATUS_CONFIG_ERROR)
#define XHCI_PORT_STATUS_RW_MASK         (XHCI_PORT_STATUS_POWER | XHCI_PORT_STATUS_RESET)

PACKED_ATYPESTRUCT(volatile, XhciRuntimeRegisters, {
    reg32_t MicroframeIndex;
    reg32_t Reserved[7];
});

PACKED_ATYPESTRUCT(volatile, XhciInterrupterRegisters, {
    reg32_t Management;
    reg32_t Moderation;
    reg32_t EventRingSegmentTableSize;
    reg32_t Reserved;
    reg64_t EventRingSegmentTableBaseAddress;
    reg64_t EventRingDequeuePointer;
});

#define XHCI_INTERRUPTER_MANAGEMENT_PENDING  (1 << 0)
#define XHCI_INTERRUPTER_MANAGEMENT_ENABLE   (1 << 1)
#define XHCI_INTERRUPTER_ERDP_BUSY           (1 << 3)

PACKED_TYPESTRUCT(XhciTransferRequestBlock, {
    reg64_t Parameter;
    reg32_t Status;
    reg32_t Control;
});

#define XHCI_TRB_CONTROL_CYCLE           (1 << 0)
#define XHCI_TRB_CONTROL_TYPE(n)         (((n) & 0x3F) << 10)
#define XHCI_TRB_TYPE_LINK               6

PACKED_TYPESTRUCT(XhciEventRingSegmentTableEntry, {
    reg64_t RingSegmentBaseAddress;
    reg32_t RingSegmentSize;
    reg32_t Reserved;
});

typedef struct XhciController {
    UsbManagerController_t Base;

    XhciCapabilityRegisters_t*    CapRegisters;
    XhciOperationalRegisters_t*   OpRegisters;
    XhciRuntimeRegisters_t*       RuntimeRegisters;
    XhciInterrupterRegisters_t*   InterrupterRegisters;
    volatile reg32_t*             Doorbells;
    XhciPortRegisters_t*          Ports;

    reg32_t HcsParams1;
    reg32_t HcsParams2;
    reg32_t HccParams1;
    size_t  SlotCount;
    size_t  ScratchpadCount;
    size_t  ContextSize;

    OSHandle_t DCBaaDMA;
    SHMSGTable_t DCBaaDMATable;
    reg64_t* DCBaa;

    OSHandle_t ScratchpadArrayDMA;
    SHMSGTable_t ScratchpadArrayDMATable;
    reg64_t* ScratchpadArray;

    OSHandle_t ScratchpadBufferDMA;
    SHMSGTable_t ScratchpadBufferDMATable;

    OSHandle_t CommandRingDMA;
    SHMSGTable_t CommandRingDMATable;
    XhciTransferRequestBlock_t* CommandRing;

    OSHandle_t EventRingDMA;
    SHMSGTable_t EventRingDMATable;
    XhciTransferRequestBlock_t* EventRing;

    OSHandle_t ErstDMA;
    SHMSGTable_t ErstDMATable;
    XhciEventRingSegmentTableEntry_t* Erst;
} XhciController_t;

extern oserr_t XhciQueueInitialize(_In_ XhciController_t* controller);
extern void    XhciQueueDestroy(_In_ XhciController_t* controller);
extern oserr_t XhciReset(_In_ XhciController_t* controller);
extern oserr_t XhciRun(_In_ XhciController_t* controller);
extern oserr_t XhciHalt(_In_ XhciController_t* controller);
extern void    XhciPortScan(_In_ XhciController_t* controller);
extern void    XhciPortClearChanges(_In_ XhciController_t* controller, _In_ size_t index, _In_ reg32_t changes);

#endif //!__USB_XHCI__

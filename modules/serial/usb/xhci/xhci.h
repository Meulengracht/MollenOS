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
#include <os/shm.h>

#include "../common/manager.h"
#include "../common/scheduler.h"
#include "../common/hci.h"

#define XHCI_MAX_PORTS             255
#define XHCI_MAX_DEVICE_SLOTS      255
#define XHCI_MAX_ENDPOINTS         32
#define XHCI_TD_ALIGNMENT          16
#define XHCI_TD_POOL               0
#define XHCI_TD_COUNT              512
#define XHCI_RING_TRB_COUNT        256
#define XHCI_TRB_MAX_DATA          65536U

PACKED_ATYPESTRUCT(volatile, XhciCapabilityRegisters, {
    uint8_t  CapLength;
    uint8_t  Reserved;
    uint16_t HciVersion;
    reg32_t  HcsParams1;
    reg32_t  HcsParams2;
    reg32_t  HcsParams3;
    reg32_t  HccParams1;
    reg32_t  DbOff;
    reg32_t  RtsOff;
    reg32_t  HccParams2;
});

PACKED_ATYPESTRUCT(volatile, XhciOperationalRegisters, {
    reg32_t UsbCommand;
    reg32_t UsbStatus;
    reg32_t PageSize;
    reg32_t Reserved0[2];
    reg32_t DeviceNotificationControl;
    reg64_t CommandRingControl;
    reg32_t Reserved1[4];
    reg64_t DeviceContextBaseAddressArrayPointer;
    reg32_t Config;
});

PACKED_ATYPESTRUCT(volatile, XhciPortRegisters, {
    reg32_t PortSc;
    reg32_t PortPmsc;
    reg32_t PortLi;
    reg32_t PortHlpmc;
});

PACKED_TYPESTRUCT(XhciTrb, {
    reg32_t ParameterLow;
    reg32_t ParameterHigh;
    reg32_t Status;
    reg32_t Control;
});

typedef struct XhciRing {
    OSHandle_t   BufferHandle;
    SHMSGTable_t BufferSGTable;
    XhciTrb_t*   Trbs;
    uintptr_t    PhysicalBase;
    uint16_t     TrbCount;
    uint16_t     EnqueueIndex;
    uint16_t     DequeueIndex;
    uint8_t      CycleState;
    uint8_t      Flags;
} XhciRing_t;

typedef struct XhciEndpoint XhciEndpoint_t;

typedef struct XhciTransferDescriptor {
    reg32_t                 BreadthLink;
    reg32_t                 DepthLink;
    UsbSchedulerObject_t    Object;
    element_t               QueueHeader;
    UsbManagerTransfer_t*   Transfer;
    XhciEndpoint_t*         Endpoint;
    uint16_t                FirstTrbIndex;
    uint16_t                LastTrbIndex;
    uint16_t                TrbCount;
    uint16_t                Reserved;
    uint32_t                BytesTransferred;
    uint8_t                 CompletionCode;
    uint8_t                 CycleState;
    uint8_t                 Flags;
    uint8_t                 Reserved1;
} XhciTransferDescriptor_t;

struct XhciEndpoint {
    element_t               Header;
    USBAddress_t            Address;
    uint8_t                 SlotId;
    uint8_t                 DeviceContextIndex;
    uint16_t                MaxPacketSize;
    uint32_t                Flags;
    list_t                  Pending;
    XhciRing_t              TransferRing;
    XhciTransferDescriptor_t* CurrentTd;
};

typedef struct XhciController {
    UsbManagerController_t       Base;
    XhciCapabilityRegisters_t*   CapRegisters;
    XhciOperationalRegisters_t*  OpRegisters;
    volatile XhciPortRegisters_t* PortRegisters;
    volatile reg32_t*            Doorbells;
    volatile uint8_t*            RuntimeRegisters;
    uint32_t                     MaxDeviceSlots;
    uint32_t                     MaxScratchpadBuffers;
    uint32_t                     ContextSize;
    list_t                       Endpoints;
    XhciRing_t                   CommandRing;
    XhciRing_t                   EventRing;
} XhciController_t;

#define XHCI_HCSPARAMS1_MAXSLOTS(n)       ((n) & 0xFF)
#define XHCI_HCSPARAMS1_MAXPORTS(n)       (((n) >> 24) & 0xFF)
#define XHCI_HCCPARAMS1_AC64              (1u << 0)
#define XHCI_HCCPARAMS1_CSZ               (1u << 2)

#define XHCI_USBCMD_RUN                   (1u << 0)
#define XHCI_USBCMD_HCRESET               (1u << 1)

#define XHCI_USBSTS_HCHALTED              (1u << 0)

#define XHCI_PORTSC_CCS                   (1u << 0)
#define XHCI_PORTSC_PED                   (1u << 1)
#define XHCI_PORTSC_PR                    (1u << 4)
#define XHCI_PORTSC_PP                    (1u << 9)
#define XHCI_PORTSC_SPEED(n)              (((n) >> 10) & 0xF)
#define XHCI_PORTSC_CSC                   (1u << 17)
#define XHCI_PORTSC_PRC                   (1u << 21)
#define XHCI_PORTSC_PLC                   (1u << 22)
#define XHCI_PORTSC_CEC                   (1u << 23)
#define XHCI_PORTSC_W1C                   (XHCI_PORTSC_CSC | XHCI_PORTSC_PRC | XHCI_PORTSC_PLC | XHCI_PORTSC_CEC)

#define XHCI_TRB_CONTROL_CYCLE            (1u << 0)
#define XHCI_TRB_CONTROL_CHAIN            (1u << 4)
#define XHCI_TRB_CONTROL_IOC              (1u << 5)
#define XHCI_TRB_CONTROL_TYPE(n)          ((n) << 10)

#define XHCI_TRB_TYPE_NORMAL              1
#define XHCI_TRB_TYPE_SETUP_STAGE         2
#define XHCI_TRB_TYPE_DATA_STAGE          3
#define XHCI_TRB_TYPE_STATUS_STAGE        4
#define XHCI_TRB_TYPE_LINK                6
#define XHCI_TRB_TYPE_EVENT_DATA          7
#define XHCI_TRB_TYPE_TRANSFER_EVENT      32

#define XHCI_TD_FLAG_QUEUED               0x01
#define XHCI_TD_FLAG_COMPLETED            0x02
#define XHCI_TD_FLAG_CANCELLED            0x04
#define XHCI_TD_FLAG_FAILED               0x08

#define XHCI_ENDPOINT_FLAG_CONFIGURED     0x01
#define XHCI_ENDPOINT_FLAG_HALTED         0x02

extern oserr_t
XhciQueueInitialize(
    _In_ XhciController_t* controller);

extern oserr_t
XhciQueueReset(
    _In_ XhciController_t* controller);

extern void
XhciQueueDestroy(
    _In_ XhciController_t* controller);

extern oserr_t
XhciRingInitialize(
    _Out_ XhciRing_t* ring,
    _In_  uint16_t    trbCount);

extern void
XhciRingDestroy(
    _In_ XhciRing_t* ring);

extern void
XhciRingReset(
    _In_ XhciRing_t* ring);

extern oserr_t
XhciRingEnqueue(
    _In_  XhciRing_t*      ring,
    _In_  const XhciTrb_t* trb,
    _Out_ uint16_t*        trbIndexOut);

extern XhciEndpoint_t*
XhciEndpointGet(
    _In_ XhciController_t* controller,
    _In_ USBAddress_t*     address);

extern XhciEndpoint_t*
XhciEndpointGetOrCreate(
    _In_ XhciController_t*     controller,
    _In_ UsbManagerTransfer_t* transfer);

extern void
XhciEndpointDestroyAll(
    _In_ XhciController_t* controller);

extern oserr_t
XhciEndpointEnqueueTransfer(
    _In_ XhciEndpoint_t*           endpoint,
    _In_ XhciTransferDescriptor_t* descriptor);

extern void
XhciEndpointDequeueTransfer(
    _In_ XhciEndpoint_t*           endpoint,
    _In_ XhciTransferDescriptor_t* descriptor);

extern oserr_t
XhciTransferPrepare(
    _In_ XhciController_t*     controller,
    _In_ UsbManagerTransfer_t* transfer,
    _In_ XhciEndpoint_t*       endpoint);

extern void
XhciTransferCleanup(
    _In_ XhciController_t*     controller,
    _In_ UsbManagerTransfer_t* transfer);

extern void
XhciEventRingDrain(
    _In_ XhciController_t* controller);

#endif //!__USB_XHCI__

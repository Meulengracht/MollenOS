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

/*
 * xHCI capability block. This is the first MMIO region exposed by the host
 * controller and describes the controller's features and offsets to the
 * operational/runtime areas. The code uses the capability data to discover the
 * supported slot count, scratchpad count, port count and the location of the
 * extended capabilities list.
 */
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
#define XHCI_HCSPARAMS2_MAXSCRATCHPADS(n) (((((n) >> 27) & 0x1F) << 5) | (((n) >> 21) & 0x1F))
#define XHCI_HCCPARAMS1_AC64             (1 << 0)
#define XHCI_HCCPARAMS1_CSZ              (1 << 2)
#define XHCI_HCCPARAMS1_XECP(n)          (((n) >> 16) & 0xFFFF)

#define XHCI_EXTCAP_ID(n)                ((n) & 0xFF)
#define XHCI_EXTCAP_NEXT(n)              (((n) >> 8) & 0xFF)
#define XHCI_EXTCAP_LEGACY_SUPPORT       1
#define XHCI_LEGSUP_BIOS_OWNED          (1 << 16)
#define XHCI_LEGSUP_OS_OWNED            (1 << 24)

/*
 * Operational registers are where the host controller is configured and started.
 * Most of the work here is to initialize the command ring, device-context base
 * address array and the interrupt management controls before the controller is
 * put into running mode.
 */
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

/*
 * Each root port exposes a small register block. We only need the status/control
 * word for detection, enablement and change bits; the other port registers are
 * retained for completeness as xHCI exposes them as part of the standard layout.
 */
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

/*
 * Runtime registers are used for interrupt management and the event ring. The
 * EventRingDequeuePointer is updated as the host drains the event ring to keep
 * the hardware and software views of the ring synchronized.
 */
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

/*
 * xHCI event ring data structures.
 *
 * Each ring entry is a TRB (transfer request block) carrying a 64-bit
 * parameter, a status field and a control word. The control word encodes the
 * operation type (link, port status change, transfer completion, etc.) and the
 * cycle bit that tells the hardware/software which ownership state the entry is
 * in.
 */

#define XHCI_INTERRUPTER_MANAGEMENT_PENDING  (1 << 0)
#define XHCI_INTERRUPTER_MANAGEMENT_ENABLE   (1 << 1)
#define XHCI_INTERRUPTER_ERDP_BUSY           (1 << 3)

PACKED_TYPESTRUCT(XhciTransferRequestBlock, {
    reg64_t Parameter;
    reg32_t Status;
    reg32_t Control;
});

typedef XhciTransferRequestBlock_t XhciTrb_t;

#define XHCI_TRB_CONTROL_CYCLE           (1 << 0)
#define XHCI_TRB_CONTROL_ISP             (1 << 2)
#define XHCI_TRB_CONTROL_CHAIN           (1 << 4)
#define XHCI_TRB_CONTROL_IOC             (1 << 5)
#define XHCI_TRB_CONTROL_IDT             (1 << 6)
#define XHCI_TRB_CONTROL_TOGGLE_CYCLE    (1 << 1)
#define XHCI_TRB_CONTROL_DIR_IN          (1 << 16)
#define XHCI_TRB_CONTROL_TRT_NONE        (0 << 16)
#define XHCI_TRB_CONTROL_TRT_OUT         (2 << 16)
#define XHCI_TRB_CONTROL_TRT_IN          (3 << 16)
#define XHCI_TRB_CONTROL_TYPE_GET(n)     (((n) >> 10) & 0x3F)
#define XHCI_TRB_CONTROL_TYPE(n)         (((n) & 0x3F) << 10)
#define XHCI_TRB_TYPE_NORMAL             1
#define XHCI_TRB_TYPE_SETUP_STAGE        2
#define XHCI_TRB_TYPE_DATA_STAGE         3
#define XHCI_TRB_TYPE_STATUS_STAGE       4
#define XHCI_TRB_TYPE_LINK               6
#define XHCI_TRB_TYPE_ENABLE_SLOT        9
#define XHCI_TRB_TYPE_ADDRESS_DEVICE     11
#define XHCI_TRB_TYPE_CONFIGURE_ENDPOINT 12
#define XHCI_TRB_TYPE_TRANSFER_EVENT     32
#define XHCI_TRB_TYPE_COMMAND_COMPLETION 33
#define XHCI_TRB_TYPE_PORT_STATUS_CHANGE 34

#define XHCI_TRB_COMPLETION_CODE(n)      (((n) >> 24) & 0xFF)
#define XHCI_TRB_TRANSFER_LENGTH(n)       ((n) & 0xFFFFFF)
#define XHCI_TRB_COMPLETION_SUCCESS      1
#define XHCI_TRB_COMPLETION_SHORT_PACKET 13
#define XHCI_TRB_SLOT_ID(n)              (((n) >> 24) & 0xFF)
#define XHCI_TRB_ADDRESS_BSR             (1 << 9)

PACKED_TYPESTRUCT(XhciEventRingSegmentTableEntry, {
    reg64_t RingSegmentBaseAddress;
    reg32_t RingSegmentSize;
    reg32_t Reserved;
});

/*
 * Generic software ring state used by the xHCI driver. This mirrors the hardware
 * ring concepts while keeping a tracked enqueue/dequeue position and cycle bit
 * for each DMA-backed ring.
 */

#define XHCI_RING_TRB_COUNT             64
#define XHCI_TD_ALIGNMENT              16
#define XHCI_TD_POOL                   0
#define XHCI_TD_COUNT                  256
#define XHCI_TRB_MAX_DATA              0x1000
#define XHCI_TD_FLAG_QUEUED            (1 << 0)
#define XHCI_TD_FLAG_COMPLETED         (1 << 1)
#define XHCI_TD_FLAG_FAILED            (1 << 2)
#define XHCI_TD_FLAG_CANCELLED         (1 << 3)

typedef struct XhciEndpoint XhciEndpoint_t;
typedef struct XhciRing XhciRing_t;
typedef struct XhciDevice XhciDevice_t;

typedef struct XhciRing {
    /* Producer rings reserve their final entry for the cycle-toggling Link TRB. */
    XhciTrb_t*        Trbs;
    uintptr_t         PhysicalBase;
    uint16_t          EnqueueIndex;
    uint16_t          DequeueIndex;
    uint16_t          TrbCount;
    uint16_t          Used;
    uint8_t           CycleState;
    uint8_t           DequeueCycleState;
    OSHandle_t        BufferHandle;
    SHMSGTable_t      BufferSGTable;
} XhciRing_t;

/*
 * Every USB transfer descriptor is backed by the shared scheduler object and
 * carries xHCI-specific status metadata. The breadth and depth links are the
 * scheduler chain links used to link descriptors into the endpoint queue, while
 * the extra fields track the transfer, endpoint, queued/finished state and the
 * TRBs that were emitted for the transfer.
 */
PACKED_TYPESTRUCT(XhciTransferDescriptor, {
    reg32_t BreadthLink;
    reg32_t DepthLink;
    UsbSchedulerObject_t Object;

    uint32_t             Flags;
    uint32_t             CompletionCode;
    uint32_t             BytesTransferred;
    uint32_t             Reserved;

    element_t            QueueHeader;
    XhciEndpoint_t*      Endpoint;
    UsbManagerTransfer_t* Transfer;
    uint16_t             FirstTrbIndex;
    uint16_t             LastTrbIndex;
    uint16_t             TrbCount;
    uint32_t             TransferLength;
    uint8_t              CycleState;
    uint8_t              Reserved2;
});

/*
 * Endpoint state used by the xHCI driver. Each endpoint tracks its USB address,
 * device-context slot mapping and the queued transfer descriptors that belong to
 * it. This allows us to maintain per-endpoint pending transfers without mixing
 * state across different pipes.
 *
 * Endpoints are configured lazily: the first transfer targeting a non-control
 * endpoint creates the XhciEndpoint_t and issues a Configure Endpoint command
 * built from the USBTransfer_t metadata carried by that transfer. The transfer
 * itself is left in WAITING until the command completes, at which point it is
 * re-queued and its TRBs are submitted. If a later transfer targets the same
 * DCI with different metadata (max packet size, interval), a drop/add
 * Configure Endpoint command is issued again before the transfer is queued.
 *
 * SuperSpeed endpoint companion descriptors (MaxBurst/Mult/BytesPerInterval,
 * stream capability) are not parsed anywhere in the stack yet, so this only
 * targets USB 2 (control/bulk/interrupt) endpoints for now.
 */
enum XhciEndpointState {
    XHCI_ENDPOINT_UNCONFIGURED,
    XHCI_ENDPOINT_CONFIGURE_PENDING,
    XHCI_ENDPOINT_RUNNING,
    XHCI_ENDPOINT_HALTED,
    XHCI_ENDPOINT_STOPPED,
    XHCI_ENDPOINT_FAILED
};

typedef struct XhciEndpoint {
    USBAddress_t              Address;
    uint8_t                   SlotId;
    uint8_t                   DeviceContextIndex;
    uint16_t                  MaxPacketSize;
    XhciRing_t                TransferRing;
    list_t                    Pending;
    element_t                 Header;
    XhciTransferDescriptor_t* CurrentTd;
    XhciDevice_t*             Device;
    XhciTransferDescriptor_t* TRBOwners[XHCI_RING_TRB_COUNT];

    /* Configuration state: tracks whether a Configure Endpoint command has
     * been issued/completed for this endpoint, and the metadata that was
     * last used to build its endpoint context so later transfers can detect
     * a mismatch and trigger a reconfigure. */
    enum XhciEndpointState State;
    uint16_t               ConfiguredMaxPacketSize;
    uint8_t                ConfiguredInterval;
} XhciEndpoint_t;

enum XhciDeviceState {
    XHCI_DEVICE_ENABLE_PENDING,
    XHCI_DEVICE_DEFAULT_PENDING,
    XHCI_DEVICE_DEFAULT,
    XHCI_DEVICE_ADDRESS_PENDING,
    XHCI_DEVICE_ADDRESSED,
    XHCI_DEVICE_FAILED
};

typedef struct XhciDevice {
    element_t             Header;
    uint8_t               HubAddress;
    uint8_t               PortAddress;
    uint8_t               SlotId;
    uint8_t               UsbAddress;
    enum XhciDeviceState  State;
    
    /* Highest Device Context Index configured so far; drives the slot
     * context's Context Entries field on subsequent Configure Endpoint
     * commands. */
    uint8_t               ContextEntries;

    XhciEndpoint_t*       DefaultEndpoint;
    UsbManagerTransfer_t* InitTransfer;
    UsbManagerTransfer_t* AddressTransfer;

    OSHandle_t            InputContextDMA;
    SHMSGTable_t          InputContextDMATable;
    uint8_t*              InputContext;

    OSHandle_t            DeviceContextDMA;
    SHMSGTable_t          DeviceContextDMATable;
    uint8_t*              DeviceContext;
} XhciDevice_t;

enum XhciCommandType {
    XHCI_COMMAND_NONE,
    XHCI_COMMAND_ENABLE_SLOT,
    XHCI_COMMAND_ADDRESS_DEVICE,
    XHCI_COMMAND_CONFIGURE_ENDPOINT
};

typedef struct XhciCommand {
    enum XhciCommandType Type;
    XhciDevice_t*        Device;
    XhciEndpoint_t*      Endpoint;
} XhciCommand_t;

/*
 * Root xHCI controller structure. This contains the hardware register mapping,
 * the DMA-backed rings used for command/event processing and the per-device
 * scratchpad/DCBAA data needed to address device contexts. The base member
 * reuses the common USB manager state so the controller can integrate with the
 * shared transaction manager and scheduler APIs.
 */
typedef struct XhciController {
    UsbManagerController_t Base;

    /* Endpoint ownership is separate from the common data-toggle hashtable. */
    list_t XhciEndpoints;
    list_t Devices;

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

    /* Device Context Base Address Array (DCBAA): points to the device context
     * array used by the controller to find device state for each slot. */
    OSHandle_t DCBaaDMA;
    SHMSGTable_t DCBaaDMATable;
    reg64_t* DCBaa;

    /* Scratchpad array and buffer pages are required for controllers that report
     * a non-zero number of scratchpad buffers. These are mapped into the DCBAA
     * entry 0 as a pointer to the scratchpad array.
     */
    OSHandle_t ScratchpadArrayDMA;
    SHMSGTable_t ScratchpadArrayDMATable;
    reg64_t* ScratchpadArray;

    OSHandle_t ScratchpadBufferDMA;
    SHMSGTable_t ScratchpadBufferDMATable;

    /* Command and event rings are the primary software/hardware scheduler
     * handshake points for the controller. The command ring accepts control TRBs
     * such as configure endpoint and reset commands, while the event ring is
     * consumed by the interrupt path to detect transfer completions and port
     * changes.
     */
    XhciRing_t CommandRing;
    XhciCommand_t Commands[XHCI_COMMAND_RING_ENTRIES];

    OSHandle_t EventRingDMA;
    SHMSGTable_t EventRingDMATable;
    XhciTransferRequestBlock_t* EventRing;
    size_t EventRingIndex;
    int    EventRingCycle;

    /* Event ring segment table (ERST): points the controller to the physical
     * event ring segment used by the interrupter.
     */
    OSHandle_t ErstDMA;
    SHMSGTable_t ErstDMATable;
    XhciEventRingSegmentTableEntry_t* Erst;
} XhciController_t;

extern oserr_t XhciQueueInitialize(_In_ XhciController_t* controller);
extern oserr_t XhciQueueReset(_In_ XhciController_t* controller);
extern void    XhciQueueDestroy(_In_ XhciController_t* controller);
extern oserr_t XhciReset(_In_ XhciController_t* controller);
extern oserr_t XhciRun(_In_ XhciController_t* controller);
extern oserr_t XhciHalt(_In_ XhciController_t* controller);
extern void    XhciPortScan(_In_ XhciController_t* controller);
extern void    XhciPortClearChanges(_In_ XhciController_t* controller, _In_ size_t index, _In_ reg32_t changes);

extern oserr_t XhciRingInitialize(_Out_ XhciRing_t* ring, _In_ uint16_t trbCount);
extern void    XhciRingDestroy(_In_ XhciRing_t* ring);
extern void    XhciRingReset(_In_ XhciRing_t* ring);
extern oserr_t XhciRingEnqueue(_In_ XhciRing_t* ring, _In_ const XhciTrb_t* trb, _Out_ uint16_t* trbIndexOut);
extern void    XhciRingRelease(_In_ XhciRing_t* ring, _In_ uint16_t trbCount);

extern XhciEndpoint_t* XhciEndpointGet(_In_ XhciController_t* controller, _In_ USBAddress_t* address);
extern XhciEndpoint_t* XhciEndpointGetOrCreate(_In_ XhciController_t* controller, _In_ UsbManagerTransfer_t* transfer);
extern void            XhciEndpointDestroyAll(_In_ XhciController_t* controller);
extern oserr_t         XhciEndpointEnqueueTransfer(_In_ XhciEndpoint_t* endpoint, _In_ XhciTransferDescriptor_t* descriptor);
extern void            XhciEndpointDequeueTransfer(_In_ XhciEndpoint_t* endpoint, _In_ XhciTransferDescriptor_t* descriptor);
extern bool            XhciEndpointMetadataMatches(_In_ XhciEndpoint_t* endpoint, _In_ UsbManagerTransfer_t* transfer);

extern oserr_t XhciTransferPrepare(_In_ XhciController_t* controller, _In_ UsbManagerTransfer_t* transfer, _In_ XhciEndpoint_t* endpoint);
extern void    XhciTransferCleanup(_In_ XhciController_t* controller, _In_ UsbManagerTransfer_t* transfer);
extern bool    XhciTransferIsSetAddress(_In_ UsbManagerTransfer_t* transfer, _Out_ uint8_t* addressOut);
extern void    XhciTransferCompleteSoftware(_In_ XhciController_t* controller, _In_ UsbManagerTransfer_t* transfer);
extern oserr_t XhciTransferSubmit(_In_ XhciController_t* controller, _In_ XhciEndpoint_t* endpoint, _In_ UsbManagerTransfer_t* transfer);
extern bool    XhciTransferHandleEvent(_In_ XhciController_t* controller, _In_ XhciTrb_t* eventTrb);

extern oserr_t XhciDeviceEnsure(_In_ XhciController_t* controller, _In_ UsbManagerTransfer_t* transfer, _In_ XhciEndpoint_t* endpoint, _Out_ XhciDevice_t** deviceOut);
extern oserr_t XhciDeviceSetAddress(_In_ XhciController_t* controller, _In_ XhciDevice_t* device, _In_ UsbManagerTransfer_t* transfer, _In_ uint8_t address);
extern oserr_t XhciDeviceConfigureEndpoint(_In_ XhciController_t* controller, _In_ XhciDevice_t* device, _In_ XhciEndpoint_t* endpoint, _In_ UsbManagerTransfer_t* transfer);
extern void    XhciDeviceDestroyAll(_In_ XhciController_t* controller);
extern void    XhciCommandHandleCompletion(_In_ XhciController_t* controller, _In_ XhciTrb_t* eventTrb);

#endif //!__USB_XHCI__

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
#include <ddk/dma.h>
#include <ddk/resources.h>
#include <os/types/handle.h>
#include <os/types/shm.h>

#include "../common/hci.h"
#include "../common/manager.h"
#include "dwc3.h"

/**
 * xHCI constants. The controller uses USB-defined MMIO register blocks, as EHCI
 * does. Scheduling uses rings and device contexts in place of the periodic and
 * asynchronous frame lists used by earlier HCIs.
 */
#define XHCI_COMMAND_RING_ENTRIES   256
#define XHCI_EVENT_RING_ENTRIES     256
#define XHCI_ERST_ENTRIES           1
#define XHCI_MAX_DEVICE_CONTEXTS    256

#define XHCI_TRB_ALIGNMENT          16
#define XHCI_CONTEXT_ALIGNMENT      64
#define XHCI_PAGE_SIZE              0x1000

/*
 * Capability registers at the start of the controller's MMIO region. These
 * describe controller features, slot/port/scratchpad counts and offsets to the
 * operational registers, runtime registers and extended capabilities list.
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
#define XHCI_HCSPARAMS2_MAXSCRATCHPADS(n) (((((n) >> 21) & 0x1F) << 5) | (((n) >> 27) & 0x1F))
#define XHCI_HCCPARAMS1_AC64             (1 << 0)
#define XHCI_HCCPARAMS1_CSZ              (1 << 2)
#define XHCI_HCCPARAMS1_XECP(n)          (((n) >> 16) & 0xFFFF)

#define XHCI_EXTCAP_ID(n)                ((n) & 0xFF)
#define XHCI_EXTCAP_NEXT(n)              (((n) >> 8) & 0xFF)
#define XHCI_EXTCAP_LEGACY_SUPPORT       1
#define XHCI_LEGSUP_BIOS_OWNED          (1 << 16)
#define XHCI_LEGSUP_OS_OWNED            (1 << 24)

/*
 * Operational registers for configuring and starting the controller. Before
 * starting it, set up the command ring, device-context base address array and
 * interrupt controls.
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
 * Root-port registers. StatusControl contains the connection, enable and change
 * bits used by the driver. The remaining members preserve the hardware layout.
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
 * Runtime registers contain the microframe counter and interrupter register
 * blocks. Update EventRingDequeuePointer as software consumes event ring entries
 * so the controller can reuse them.
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
 * Each ring entry is a TRB (transfer request block) with a 64-bit parameter,
 * status and control word. Control identifies the operation (link, port status
 * change, transfer completion, etc.). The cycle bit tells the consumer whether
 * the entry is ready to process.
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
#define XHCI_TRB_CONTROL_EP_ID(n)        (((n) & 0x1F) << 16)
#define XHCI_TRB_CONTROL_SLOT_ID(n)      (((n) & 0xFF) << 24)
#define XHCI_TRB_PARAMETER_DCS           (1ull << 0)
#define XHCI_TRB_TYPE_NORMAL             1
#define XHCI_TRB_TYPE_SETUP_STAGE        2
#define XHCI_TRB_TYPE_DATA_STAGE         3
#define XHCI_TRB_TYPE_STATUS_STAGE       4
#define XHCI_TRB_TYPE_LINK               6
#define XHCI_TRB_TYPE_ENABLE_SLOT        9
#define XHCI_TRB_TYPE_ADDRESS_DEVICE     11
#define XHCI_TRB_TYPE_CONFIGURE_ENDPOINT 12
#define XHCI_TRB_TYPE_EVALUATE_CONTEXT   13
#define XHCI_TRB_TYPE_RESET_ENDPOINT     14
#define XHCI_TRB_TYPE_STOP_ENDPOINT      15
#define XHCI_TRB_TYPE_SET_TR_DEQUEUE     16
#define XHCI_TRB_TYPE_DISABLE_SLOT       10
#define XHCI_TRB_TYPE_TRANSFER_EVENT     32
#define XHCI_TRB_TYPE_COMMAND_COMPLETION 33
#define XHCI_TRB_TYPE_PORT_STATUS_CHANGE 34

#define XHCI_TRB_COMPLETION_CODE(n)      (((n) >> 24) & 0xFF)
#define XHCI_TRB_TRANSFER_LENGTH(n)       ((n) & 0xFFFFFF)
#define XHCI_TRB_COMPLETION_SUCCESS      1
#define XHCI_TRB_COMPLETION_SHORT_PACKET 13
#define XHCI_TRB_SLOT_ID(n)              (((n) >> 24) & 0xFF)
#define XHCI_TRB_ENDPOINT_ID(n)          (((n) >> 16) & 0x1F)
#define XHCI_TRB_ADDRESS_BSR             (1 << 9)
#define XHCI_SLOT_CONTEXT_ROOT_PORT(n)   (((n) & 0xFF) << 16)
#define XHCI_SLOT_CONTEXT_NUM_PORTS(n)   (((n) & 0xFF) << 24)
#define XHCI_SLOT_CONTEXT_TT_HUB(n)      ((n) & 0xFF)
#define XHCI_SLOT_CONTEXT_TT_PORT(n)     (((n) & 0xFF) << 8)
#define XHCI_SLOT_CONTEXT_TT_THINK(n)    (((n) & 0x3) << 16)
#define XHCI_SLOT_CONTEXT_HUB            (1 << 26)
#define XHCI_SLOT_CONTEXT_MTT            (1 << 25)

PACKED_TYPESTRUCT(XhciEventRingSegmentTableEntry, {
    reg64_t RingSegmentBaseAddress;
    reg32_t RingSegmentSize;
    reg32_t Reserved;
});

/*
 * Software ring state tracks enqueue/dequeue positions and cycle bits for each
 * ring in DMA memory.
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

struct XhciScratchpad {
    struct DMABuffer* Buffer;
};

/**
 * @brief Allocate scratchpad pages without requiring contiguous physical memory.
 *
 * @param context Device DMA context.
 * @param count Number of page pointers in addresses.
 * @param addresses Coherent hardware pointer array.
 * @param pages Must point to a NULL slot. Receives the allocated pages, including
 *              any pages left to free if allocation fails.
 * @return Allocation status. On failure, call XhciScratchpadsFree to release any
 *         pages already allocated.
 */
extern oserr_t
XhciScratchpadsAllocate(
    _In_    struct DMAContext*      context,
    _In_    size_t                  count,
    _Out_   reg64_t*                addresses,
    _InOut_ struct XhciScratchpad** pages);

/**
 * @brief Release scratchpad pages. Hardware must be stopped before calling.
 *
 * @param pages Array of pages to release. Cleared after all pages are released.
 * @param count Number of entries allocated in the array.
 * @return Release status. Failed entries stay in the array so release can be retried.
 */
extern oserr_t
XhciScratchpadsFree(
    _InOut_ struct XhciScratchpad** pages,
    _In_    size_t                  count);

/**
 * @brief Allocate coherent controller memory and its translated device address.
 *
 * @param context Device DMA context.
 * @param length Required bytes, with page alignment and ring boundary constraints.
 * @param buffer Receives the shared allocation to be released by the caller.
 * @param deviceAddress Receives the contiguous device base address.
 * @param memory Optional CPU address output.
 * @return Allocation status. Failure leaves the allocation slot unchanged.
 */
extern oserr_t
XhciDmaAllocate(
    _In_    struct DMAContext*   context,
    _In_    size_t               length,
    _InOut_ struct DMABuffer**   buffer,
    _Out_   uint64_t*            deviceAddress,
    _Out_   void**               memory);


typedef struct XhciRing {
    /* Producer rings reserve their final entry for the cycle-toggling Link TRB. */
    XhciTrb_t*        Trbs;
    uint64_t          DeviceBase;
    uint16_t          EnqueueIndex;
    uint16_t          DequeueIndex;
    uint16_t          TrbCount;
    uint16_t          Used;
    uint8_t           CycleState;
    uint8_t           DequeueCycleState;
    struct DMABuffer*   BufferHandle;
} XhciRing_t;

/*
 * Software transfer descriptor. Pointers and list links use natural alignment.
 * Hardware reads separate TRB rings. BreadthLink and DepthLink chain scheduler
 * objects into the endpoint queue. The remaining fields track the USB transfer,
 * endpoint, completion state and the TRBs submitted for the transfer.
 */
typedef struct XhciTransferDescriptor {
    reg32_t BreadthLink;
    reg32_t DepthLink;
    UsbSchedulerObject_t Object;

    uint32_t             Flags;
    uint32_t             CompletionCode;
    uint32_t             BytesTransferred;
    uint32_t             Reserved;
    element_t            QueueHeader;
    
    struct XhciTransferDescriptor* CacheNext;
    XhciEndpoint_t*                Endpoint;
    UsbManagerTransfer_t*          Transfer;
    uint16_t                       FirstTrbIndex;
    uint16_t                       LastTrbIndex;
    uint16_t                       TrbCount;
    uint32_t                       TransferLength;
    uint8_t                        CycleState;
    uint8_t                        Reserved2;
} XhciTransferDescriptor_t;

/*
 * Endpoint state tracks the USB address, device-context slot mapping and pending
 * transfer descriptors for one pipe.
 *
 * The first transfer to a non-control endpoint creates the XhciEndpoint_t and
 * submits Configure Endpoint using the transfer's USBTransfer_t metadata. The
 * transfer stays in WAITING until the command completes, then is requeued and
 * its TRBs are submitted. If a later transfer uses the same DCI with a different
 * max packet size or interval, a drop/add Configure Endpoint command must
 * complete before that transfer is queued.
 *
 * SuperSpeed endpoint companion descriptors (MaxBurst/Mult/BytesPerInterval,
 * stream capability) are not parsed by the stack yet. Only USB 2 control, bulk
 * and interrupt endpoints are supported here.
 */
enum XhciEndpointState {
    XHCI_ENDPOINT_UNCONFIGURED,
    XHCI_ENDPOINT_CONFIGURE_PENDING,
    XHCI_ENDPOINT_RUNNING,
    XHCI_ENDPOINT_HALTED,
    XHCI_ENDPOINT_STOPPED,
    /* Recovery waits for Reset/Stop Endpoint to complete, then submits Set TR
     * Dequeue Pointer. That command must complete before rebuilding the ring
     * and requeuing pending transfers. */
    XHCI_ENDPOINT_RESET_PENDING,
    XHCI_ENDPOINT_STOP_PENDING,
    XHCI_ENDPOINT_DEQUEUE_PENDING,
    // Hardware is stopped. Complete DMA for every payload so the CPU can access
    // it before restarting the ring.
    XHCI_ENDPOINT_REBUILD_PENDING,
    // Hardware is stopped. Complete DMA and fail every pending transfer.
    XHCI_ENDPOINT_RETIRE_PENDING,
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

    /* Configuration state and metadata last used to build the endpoint context.
     * Later transfers must reconfigure the endpoint if this metadata differs. */
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
    XHCI_DEVICE_DETACHING,
    // Disable Slot succeeded. Retry releasing the remaining software resources.
    XHCI_DEVICE_RELEASE_PENDING,
    XHCI_DEVICE_FAILED
};

/**
 * Device enumeration state and DMA contexts for one attached USB device.
 * Endpoints points to entries in the controller's endpoint list. Completion
 * handling indexes it by the hardware Device Context Index.
 */
typedef struct XhciDevice {
    element_t             Header;
    uint8_t               HubAddress;
    uint8_t               PortAddress;
    uint8_t               SlotId;
    uint8_t               UsbAddress;
    uint8_t               Speed;
    uint8_t               RootPort;
    uint8_t               Depth;
    uint8_t               DetachPending;
    uint32_t              RouteString;
    XhciDevice_t*         Parent;
    uint8_t               HubPortCount;
    uint16_t              HubCharacteristics;
    enum XhciDeviceState  State;
    
    /* Highest Device Context Index configured so far. Used for the slot
     * context's Context Entries field in later Configure Endpoint commands. */
    uint8_t               ContextEntries;

    XhciEndpoint_t*       DefaultEndpoint;
    // Device Context Index zero is the slot context, never an endpoint.
    XhciEndpoint_t*       Endpoints[32];
    UsbManagerTransfer_t* InitTransfer;
    UsbManagerTransfer_t* AddressTransfer;

    struct DMABuffer*     InputContextDMA;
    uint64_t              InputContextDeviceAddress;
    uint8_t*              InputContext;

    struct DMABuffer*     DeviceContextDMA;
    uint64_t              DeviceContextDeviceAddress;
    uint8_t*              DeviceContext;
} XhciDevice_t;

enum XhciCommandType {
    XHCI_COMMAND_NONE,
    XHCI_COMMAND_ENABLE_SLOT,
    XHCI_COMMAND_ADDRESS_DEVICE,
    XHCI_COMMAND_CONFIGURE_ENDPOINT,
    XHCI_COMMAND_EVALUATE_CONTEXT,
    XHCI_COMMAND_RESET_ENDPOINT,
    XHCI_COMMAND_STOP_ENDPOINT,
    XHCI_COMMAND_SET_TR_DEQUEUE,
    XHCI_COMMAND_DISABLE_SLOT
};

typedef struct XhciCommand {
    enum XhciCommandType Type;
    XhciDevice_t*        Device;
    XhciEndpoint_t*      Endpoint;
} XhciCommand_t;

enum XhciControllerState {
    XHCI_CONTROLLER_RUNNING,
    XHCI_CONTROLLER_STOPPING,
    XHCI_CONTROLLER_HALTED
};

/*
 * Controller register mappings, command/event rings and scratchpad/DCBAA data.
 * Base holds the common USB manager state used by the transaction manager and
 * scheduler APIs.
 */
typedef struct XhciController {
    UsbManagerController_t   Base;
    DeviceIo_t               RegisterIo;
    // The protocol window may be smaller than the integration's mapping.
    size_t                   RegisterLength;
    struct DWC3State         Dwc3;
    bool                     Prepared;
    bool                     RecoveryPending;
    // STOPPING rejects work. Once HALTED, teardown retries skip the MMIO halt.
    enum XhciControllerState State;

    /* Endpoint objects are tracked separately from the common data-toggle hashtable. */
    list_t XhciEndpoints;
    list_t Devices;

    // Hardware slot IDs are eight bits. Zero is reserved. Entries point to the
    // device until Disable Slot completes or controller teardown releases it.
    XhciDevice_t* Slots[256];

    XhciCapabilityRegisters_t*  CapRegisters;
    XhciOperationalRegisters_t* OpRegisters;
    XhciRuntimeRegisters_t*     RuntimeRegisters;
    XhciInterrupterRegisters_t* InterrupterRegisters;
    volatile reg32_t*           Doorbells;
    XhciPortRegisters_t*        Ports;

    reg32_t HcsParams1;
    reg32_t HcsParams2;
    reg32_t HccParams1;
    size_t  SlotCount;
    size_t  ScratchpadCount;
    size_t  ContextSize;

    /* Device Context Base Address Array (DCBAA): points to the device context
     * array used by the controller to find device state for each slot. */
    struct DMABuffer* DCBaaDMA;
    uint64_t          DCBaaDeviceAddress;
    reg64_t*          DCBaa;

    /* If the controller reports a non-zero scratchpad count, allocate the array
     * and buffer pages. DCBAA entry 0 points to the scratchpad array.
     */
    struct DMABuffer*      ScratchpadArrayDMA;
    uint64_t               ScratchpadArrayDeviceAddress;
    reg64_t*               ScratchpadArray;
    struct XhciScratchpad* Scratchpads;

    /* Software submits commands such as Configure Endpoint and Reset Endpoint
     * through the command ring. The interrupt handler reads the event ring for
     * transfer completions and port changes.
     */
    XhciRing_t    CommandRing;
    XhciCommand_t Commands[XHCI_COMMAND_RING_ENTRIES];

    struct DMABuffer*           EventRingDMA;
    uint64_t                    EventRingDeviceAddress;
    XhciTransferRequestBlock_t* EventRing;
    size_t                      EventRingIndex;
    int                         EventRingCycle;

    /* Event ring segment table (ERST): points the controller to the physical
     * event ring segment used by the interrupter.
     */
    struct DMABuffer*                 ErstDMA;
    uint64_t                          ErstDeviceAddress;
    XhciEventRingSegmentTableEntry_t* Erst;
} XhciController_t;

extern oserr_t XhciQueueInitialize(_In_ XhciController_t* controller);
extern oserr_t XhciQueueReset(_In_ XhciController_t* controller);
extern oserr_t XhciQueueDestroy(_In_ XhciController_t* controller);
extern oserr_t XhciReset(_In_ XhciController_t* controller);
extern oserr_t XhciRun(_In_ XhciController_t* controller);
extern oserr_t XhciHalt(_In_ XhciController_t* controller);
/**
 * @brief Report root-port connection changes to the USB service.
 * @param controller Registered controller whose ports are powered.
 * @param initial Also report connected ports without a pending change at startup.
 */
extern void
XhciPortScan(
    _In_    XhciController_t* controller,
    _In_    bool              initial);

extern void XhciPortClearChanges(_In_ XhciController_t* controller, _In_ size_t index, _In_ reg32_t changes);

extern oserr_t XhciRingInitialize(_In_ struct DMAContext* context, _Out_ XhciRing_t* ring, _In_ uint16_t trbCount);
extern oserr_t XhciRingDestroy(_In_ XhciRing_t* ring);
extern void    XhciRingReset(_In_ XhciRing_t* ring);
extern oserr_t XhciRingEnqueue(_In_ XhciRing_t* ring, _In_ const XhciTrb_t* trb, _Out_ uint16_t* trbIndexOut);
extern void    XhciRingRelease(_In_ XhciRing_t* ring, _In_ uint16_t trbCount);

extern XhciEndpoint_t* XhciEndpointGet(_In_ XhciController_t* controller, _In_ USBAddress_t* address);
extern XhciEndpoint_t* XhciEndpointGetOrCreate(_In_ XhciController_t* controller, _In_ UsbManagerTransfer_t* transfer);
extern oserr_t         XhciEndpointDestroyAll(_In_ XhciController_t* controller);
extern oserr_t         XhciEndpointDestroyDevice(_In_ XhciController_t* controller, _In_ XhciDevice_t* device);
extern oserr_t         XhciEndpointEnqueueTransfer(_In_ XhciEndpoint_t* endpoint, _In_ XhciTransferDescriptor_t* descriptor);
extern void            XhciEndpointDequeueTransfer(_In_ XhciEndpoint_t* endpoint, _In_ XhciTransferDescriptor_t* descriptor);
extern bool            XhciEndpointMetadataMatches(_In_ XhciEndpoint_t* endpoint, _In_ UsbManagerTransfer_t* transfer);

extern oserr_t XhciTransferPrepare(_In_ XhciController_t* controller, _In_ UsbManagerTransfer_t* transfer, _In_ XhciEndpoint_t* endpoint);
extern void    XhciTransferCleanup(_In_ XhciController_t* controller, _In_ UsbManagerTransfer_t* transfer);
extern bool    XhciTransferIsSetAddress(_In_ UsbManagerTransfer_t* transfer, _Out_ uint8_t* addressOut);
extern void    XhciTransferCompleteSoftware(_In_ XhciController_t* controller, _In_ UsbManagerTransfer_t* transfer);
extern void    XhciTransferMarkCancelled(_In_ XhciController_t* controller, _In_ UsbManagerTransfer_t* transfer);
extern oserr_t XhciTransferSubmit(_In_ XhciController_t* controller, _In_ XhciEndpoint_t* endpoint, _In_ UsbManagerTransfer_t* transfer);
extern bool    XhciTransferHandleEvent(_In_ XhciController_t* controller, _In_ XhciTrb_t* eventTrb);

extern oserr_t XhciDeviceEnsure(_In_ XhciController_t* controller, _In_ UsbManagerTransfer_t* transfer, _In_ XhciEndpoint_t* endpoint, _Out_ XhciDevice_t** deviceOut);
extern oserr_t XhciDeviceSetAddress(_In_ XhciController_t* controller, _In_ XhciDevice_t* device, _In_ UsbManagerTransfer_t* transfer, _In_ uint8_t address);
extern oserr_t XhciDeviceConfigureEndpoint(_In_ XhciController_t* controller, _In_ XhciDevice_t* device, _In_ XhciEndpoint_t* endpoint, _In_ UsbManagerTransfer_t* transfer);
extern oserr_t XhciDeviceDestroyAll(_In_ XhciController_t* controller);
extern void    XhciCommandHandleCompletion(_In_ XhciController_t* controller, _In_ XhciTrb_t* eventTrb);
extern oserr_t XhciEndpointReset(_In_ XhciController_t* controller, _In_ XhciEndpoint_t* endpoint);
extern oserr_t XhciEndpointCancelTransfer(_In_ XhciController_t* controller, _In_ XhciEndpoint_t* endpoint, _In_ UsbManagerTransfer_t* transfer);

#endif //!__USB_XHCI__

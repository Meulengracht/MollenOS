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
 *
 *
 * Open Host Controller Interface Driver
 * TODO:
 *    - Power Management
 */

#ifndef __USB_OHCI__
#define __USB_OHCI__

#include <os/shm.h>

#include "../common/manager.h"
#include "../common/scheduler.h"
#include "../common/hci.h"

/* OHCI Controller Definitions 
 * Contains generic magic constants and definitions */
#define OHCI_MAXPORTS                   15
#define OHCI_FRAMELIST_SIZE             32

#define OHCI_LINK_HALTED                0x1

/* OhciQueueHead
 * Endpoint descriptor, which acts as a queue-head in an
 * ohci-context. It contains both horizontal links and vertical
 * links. It will always process vertical links first. When a 
 * vertical link has processed, the current is updated. 
 * It will stop processing when Current == Tail.
 * Structure must always be 16 byte aligned */
PACKED_TYPESTRUCT(OhciQueueHead, {
    // Hardware Metadata (16 bytes)
    reg32_t                 Flags;
    reg32_t                 EndPointer;  // Lower 4 bits not used
    reg32_t                 Current;     // (Physical) Bit 0 - Halted, Bit 1 - Carry
    reg32_t                 LinkPointer; // Next EP (Physical)
    
    // Software meta-data
    UsbSchedulerObject_t    Object;
});

/**
 * OhciQueueHead::Flags
 * Contains definitions and bitfield definitions for OhciQueueHead::Flags
 * Bits 0 - 6: Usb Address of Function
 * Bits 7 - 10: (Endpoint Nr) Usb Address of this endpoint
 * Bits 11 - 12: (Direction) Indicates Dataflow, either IN or OUT. (01 -> OUT, 10 -> IN). Otherwise we leave it to the TD descriptor
 * Bits 13: Speed. 0 indicates full speed, 1 indicates low-speed.
 * Bits 14: If set, it skips the TD queues and moves on to next EP descriptor
 * Bits 15: (Format). 0 = Bulk/Control/Interrupt Endpoint. 1 = Isochronous TD format.
 * Bits 16-26: Maximum Packet Size per data packet.
 * Bits 27-31: Type -> 0000 (Control), 0001 (Bulk), 0010 (Interrupt) 0011 (Isoc).
 */
#define OHCI_QH_ADDRESSMASK             0x7F
#define OHCI_QH_ENDPOINTMASK            0x0F
#define OHCI_QH_LENGTHMASK              0x3FF

#define OHCI_QH_ADDRESS(Address)        (Address & OHCI_QH_ADDRESSMASK)
#define OHCI_QH_ENDPOINT(Endpoint)      ((Endpoint & OHCI_QH_ENDPOINTMASK) << 7)
#define OHCI_QH_OUT                     (1 << 11)
#define OHCI_QH_IN                      (1 << 12)
#define OHCI_QH_DIRECTIONTD             (OHCI_QH_OUT | OHCI_QH_IN)
#define OHCI_QH_LOWSPEED                (1 << 13)
#define OHCI_QH_SKIP                    (1 << 14)
#define OHCI_QH_ISOCHRONOUS             (1 << 15)
#define OHCI_QH_LENGTH(Length)          ((Length & OHCI_QH_LENGTHMASK) << 16)
#define OHCI_QH_TYPE(Type)              (((int)Type & 0xF) << 27)

// 16 byte alignment
PACKED_TYPESTRUCT(OhciTransferDescriptor, {
    reg32_t Flags;
    reg32_t Cbp;
    reg32_t Link;
    reg32_t BufferEnd;

    // Software Metadata
    UsbSchedulerObject_t Object;
    reg32_t              OriginalFlags;      // Copy of flags
    reg32_t              OriginalCbp;        // Copy of buffer
});

/**
 * OhciTransferDescriptor::Flags & OhciIsocTransferDescriptor::Flags
 * Contains definitions and bitfield definitions for OhciTransferDescriptor::Flags
 * Bits 0-17:  Available
 * Bits 18:    If 0, Requires the data to be recieved from an endpoint to exactly fill buffer
 * Bits 19-20: Direction, 00 = Setup (to ep), 01 = OUT (to ep), 10 = IN (from ep)
 * Bits 21-23: Interrupt delay count for this TD. This means the HC can delay interrupt a specific amount of frames after TD completion.
 * Bits 24-25: Data Toggle. It is updated after each successful transmission.
 * Bits 26-27: Error Count. Updated each transmission that fails. It is 0 on success.
 * Bits 28-31: Condition Code, if error count is 2 and it fails a third time, this contains error code.
 */
#define OHCI_TD_SHORTPACKET_OK          (1 << 18)
#define OHCI_TD_SETUP                   0
#define OHCI_TD_OUT                     (1 << 19)
#define OHCI_TD_IN                      (1 << 20)
#define OHCI_TD_IOC_NONE                ((1 << 21) | (1 << 22) | (1 << 23))

// Generic
#define OHCI_TD_TOGGLE                  (1 << 24)
#define OHCI_TD_TOGGLE_LOCAL            (1 << 25)
#define OHCI_TD_ERRORCOUNT(Flags)       ((Flags >> 26) & 0x3)
#define OHCI_TD_ERRORCODE(Flags)        ((Flags >> 28) & 0xF)
#define OHCI_TD_ACTIVE                  ((1 << 28) | (1 << 29) | (1 << 30) | (1 << 31))

// 32 byte alignment
PACKED_TYPESTRUCT(OhciIsocTransferDescriptor, {
    reg32_t  Flags;
    reg32_t  Cbp;
    reg32_t  Link;
    reg32_t  BufferEnd;
    uint16_t Offsets[8];

    // Software Metadata
    UsbSchedulerObject_t Object;
    reg32_t              OriginalFlags;      // Copy of flags
    reg32_t              OriginalCbp;        // Copy of buffer
    reg32_t              OriginalBufferEnd;  // Copy of buffer end
    uint16_t             OriginalOffsets[8]; // Copy of offsets
});

#define OHCI_iTD_FRAMECOUNT(n)           ((n & 0x7) << 24)

/**
 * OhciTransferDescriptor::Offsets
 * Contains definitions and bitfield definitions for OhciTransferDescriptor::Offsets
 * Bits 0-11:    Packet Size on IN-transmissions
 * Bits 12:      CrossPage Field
 * Bits 12-15:   Condition Code (Error Code) 
 */
#define OHCI_iTD_OFFSETLENGTH(Length) (Length & 0xFFF)
#define OHCI_iTD_CROSSPAGE            (1 << 12)
#define OHCI_iTD_OFFSETCODE(Flags)    ((Flags >> 12) & 0xF)

/**
 * @brief Condition codes for packets
 */
#define OHCI_CC_SUCCESS    0x0
#define OHCI_CC_BABBLE1    0x1
#define OHCI_CC_BABBLE2    0x2
#define OHCI_CC_DTM        0x3 // DataToggleMismatch
#define OHCI_CC_STALLED    0x4
#define OHCI_CC_NORESPONSE 0x5
#define OHCI_CC_INIT0      0xE // Not Accessed
#define OHCI_CC_INIT       0xF // Not Accessed

PACKED_ATYPESTRUCT(volatile, OhciHCCA, {
    reg32_t  InterruptTable[OHCI_FRAMELIST_SIZE]; // Points to the 32 root nodes
    uint16_t CurrentFrame;       // Current Frame Number
    uint16_t Padding;
    uint32_t HeadDone;           // Indicates which head is done
    uint8_t  Reserved[116];      // Work area of OHCI
});

PACKED_ATYPESTRUCT(volatile, OhciRegisters, {
    reg32_t HcRevision;
    reg32_t HcControl;
    reg32_t HcCommandStatus;
    reg32_t HcInterruptStatus;
    reg32_t HcInterruptEnable;
    reg32_t HcInterruptDisable;

    reg32_t HcHCCA;
    reg32_t HcPeriodCurrentED;
    reg32_t HcControlHeadED;
    reg32_t HcControlCurrentED;
    reg32_t HcBulkHeadED;
    reg32_t HcBulkCurrentED;
    reg32_t HcDoneHead;

    reg32_t HcFmInterval;
    reg32_t HcFmRemaining;
    reg32_t HcFmNumber;
    reg32_t HcPeriodicStart;
    reg32_t HcLSThreshold;

    reg32_t HcRhDescriptorA;
    reg32_t HcRhDescriptorB;
    reg32_t HcRhStatus;
    reg32_t HcRhPortStatus[OHCI_MAXPORTS];
});

/* OhciRegisters::HcRevision
 * Contains definitions and bitfield definitions for OhciRegisters::HcRevision */
#define OHCI_REVISION1                  0x10
#define OHCI_REVISION11                 0x11

/* OhciRegisters::HcCommandStatus
 * Contains definitions and bitfield definitions for OhciRegisters::HcCommandStatus */
#define OHCI_COMMAND_RESET              (1 << 0)
#define OHCI_COMMAND_CONTROL_FILLED     (1 << 1)
#define OHCI_COMMAND_BULK_FILLED        (1 << 2)
#define OHCI_COMMAND_OWNERSHIP          (1 << 3)

/* OhciRegisters::HcControl
 * Contains definitions and bitfield definitions for OhciRegisters::HcControl */
#define OHCI_CONTROL_RESET              0
#define OHCI_CONTROL_RATIO_MASK         (1 << 0) | (1 << 1)
#define OHCI_CONTROL_PERIODIC_ACTIVE    (1 << 2)
#define OHCI_CONTROL_ISOC_ACTIVE        (1 << 3)
#define OHCI_CONTROL_CONTROL_ACTIVE     (1 << 4)
#define OHCI_CONTROL_BULK_ACTIVE        (1 << 5)
#define OHCI_CONTROL_ALL_ACTIVE         ((1 << 2) | (1 << 3) | (1 << 4) | (1 << 5))
#define OHCI_CONTROL_RESUME             (1 << 6)
#define OHCI_CONTROL_ACTIVE             (1 << 7)
#define OHCI_CONTROL_SUSPEND            ((1 << 6) | (1 << 7))
#define OHCI_CONTROL_STATE_MASK         (1 << 6) | (1 << 7)
#define OHCI_CONTROL_IR                 (1 << 8)
#define OHCI_CONTROL_REMOTEWAKE         (1 << 10)

/* OhciRegisters::HcFmInterval
 * Contains definitions and bitfield definitions for OhciRegisters::HcFmInterval */
#define OHCI_FMINTERVAL_FI              0x2EDF
#define OHCI_FMINTERVAL_FIMASK          0x3FFF
#define OHCI_FMINTERVAL_GETFSMP(fi)     ((fi >> 16) & 0x7FFF)
#define OHCI_FMINTERVAL_FSMP(fi)        (0x7FFF & ((6 * ((fi) - 210)) / 7))

#define OHCI_OVERRUN_EVENT              (1 << 0)
#define OHCI_PROCESS_EVENT              (1 << 1)
#define OHCI_SOF_EVENT                  (1 << 2)
#define OHCI_RESUMEDETECT_EVENT         (1 << 3)
#define OHCI_FATAL_EVENT                (1 << 4)
#define OHCI_OVERFLOW_EVENT             (1 << 5)
#define OHCI_ROOTHUB_EVENT              (1 << 6)
#define OHCI_OWNERSHIP_EVENT            (1 << 30)
#define OHCI_MASTER_INTERRUPT           (1 << 31)

#define OHCI_DESCRIPTORA_DEVICETYPE     (1 << 10)

#define OHCI_STATUS_POWER_ENABLED       (1 << 16)

#define OHCI_PORT_CONNECTED             (1 << 0)
#define OHCI_PORT_ENABLED               (1 << 1)
#define OHCI_PORT_SUSPENDED             (1 << 2)
#define OHCI_PORT_OVERCURRENT           (1 << 3)
#define OHCI_PORT_RESET                 (1 << 4)
#define OHCI_PORT_POWER                 (1 << 8)
#define OHCI_PORT_LOW_SPEED             (1 << 9)
#define OHCI_PORT_CONNECT_EVENT         (1 << 16)
#define OHCI_PORT_ENABLE_EVENT          (1 << 17)
#define OHCI_PORT_SUSPEND_EVENT         (1 << 18)
#define OHCI_PORT_OVERCURRENT_EVENT     (1 << 19)
#define OHCI_PORT_RESET_EVENT           (1 << 20)

/* Ohci Definitions
 * Pool sizes and helper functions. */
#define OHCI_QH_ALIGNMENT                   16
#define OHCI_QH_POOL                        0
#define OHCI_QH_COUNT                       50

#define OHCI_TD_ALIGNMENT                   16
#define OHCI_TD_POOL                        1
#define OHCI_TD_COUNT                       400

#define OHCI_iTD_ALIGNMENT                  32
#define OHCI_iTD_POOL                       2
#define OHCI_iTD_COUNT                      50

#define OHCI_QH_NULL                        0
#define OHCI_QH_START                       1

#define OHCI_TD_NULL                        0
#define OHCI_TD_START                       1

#define OHCI_iTD_NULL                       0
#define OHCI_iTD_START                      1

enum OHCIPowerMode {
    OHCIPOWERMODE_ALWAYSON,
    OHCIPOWERMODE_PORTCONTROL,
    OHCIPOWERMODE_GLOBALCONTROL
};

typedef struct OHCIController {
    UsbManagerController_t Base;

    // Transactions
    reg32_t  QueuesActive;
    int      TransactionsWaitingControl;
    int      TransactionsWaitingBulk;
    uint16_t TransactionQueueControlIndex;
    uint16_t TransactionQueueBulkIndex;

    // Registers and resources
    OhciRegisters_t* Registers;
    OSHandle_t       HccaDMA;
    SHMSGTable_t     HccaDMATable;
    OhciHCCA_t*      Hcca;

    // State information
    unsigned int       PowerOnDelayMs;
    enum OHCIPowerMode PowerMode;
} OhciController_t;

/*******************************************************************************
 * Controller Methods
 *******************************************************************************/

/**
 * @brief Resets the controller state back to a known, initialized state.
 * @param controller
 * @return
 */
extern oserr_t
OHCIReset(
        _In_ OhciController_t* controller);

/**
 * @brief Initializes queue memory and queue registers.
 * @param controller
 * @return
 */
extern oserr_t
OHCIQueueInitialize(
        _In_ OhciController_t* controller);

/**
 * @brief Resets any transfers already in queue, and resets queue memory
 * back to an initialized state.
 * @param controller
 * @return
 */
extern oserr_t
OHCIQueueReset(
    _In_ OhciController_t* controller);

/**
 * @brief Cleans up any queued transfers and frees resources used by the queue.
 * @param Controller
 */
extern void
OHCIQueueDestroy(
    _In_ OhciController_t* Controller);

/**
 * @brief Checks the current status of all ports and raises any neccessary events.
 * @param controller
 * @param ignorePowerOn
 * @return
 */
extern oserr_t
OHCICheckPorts(
    _In_ OhciController_t* controller,
    _In_ int               ignorePowerOn);

/**
 * @brief Change current controller state.
 * @param controller
 * @param mode
 */
extern void
OHCISetMode(
    _In_ OhciController_t* controller,
    _In_ reg32_t           mode);

/*******************************************************************************
 * Queue Head Methods
 *******************************************************************************/

/* OHCIQHInitialize
 * Initializes the queue head data-structure and the associated
 * hcd flags. Afterwards the queue head is ready for use. */
extern oserr_t
OHCIQHInitialize(
        _In_ OhciController_t*     controller,
        _In_ UsbManagerTransfer_t* transfer,
        _In_ OhciQueueHead_t*      qh);

/* OHCIQHDump
 * Dumps the information contained in the queue-head by writing it to stdout */
extern void
OHCIQHDump(
    _In_ OhciController_t*          Controller,
    _In_ OhciQueueHead_t*           Qh);

/* OHCIQHRestart
 * Restarts an interrupt QH by resetting it to it's start state */
extern void
OHCIQHRestart(
    _In_ OhciController_t*          Controller,
    _In_ UsbManagerTransfer_t*      Transfer);

/* OHCIQHLink
 * Link a given queue head into the correct queue determined by Qh->Queue.
 * This can handle linkage of async and interrupt transfers. */
extern void
OHCIQHLink(
    _In_ OhciController_t*          controller,
    _In_ uint8_t          type,
    _In_ OhciQueueHead_t*           qh);

/*******************************************************************************
 * Transfer Descriptor Methods
 *******************************************************************************/

/**
 * @brief Constructs a new SETUP token TD. The TD assumes a length
 * of sizeof(usbpacket_t).
 */
extern void
OHCITDSetup(
    _In_ OhciTransferDescriptor_t* td,
    _In_ struct TransferElement*   element);

/* OHCITDData
 * Creates a new io token td and initializes all the members.
 * The Td is immediately ready for execution. */
extern void
OHCITDData(
    _In_ OhciTransferDescriptor_t*  td,
    _In_ enum USBTransferType       type,
    _In_ uint32_t                   pid,
    _In_ struct TransferElement*    element,
    _In_ int                        toggle);

/* OHCITDDump
 * Dumps the information contained in the descriptor by writing it. */
extern void
OHCITDDump(
    _In_ OhciController_t*          controller,
    _In_ OhciTransferDescriptor_t*  td);

/* OHCITDVerify
 * Checks the transfer descriptors for errors and updates the transfer that is attached
 * with the bytes transferred and error status. */
extern void
OHCITDVerify(
    _In_ struct HCIProcessReasonScanContext* scanContext,
    _In_ OhciTransferDescriptor_t*           td);

/* OHCITDRestart
 * Restarts a transfer descriptor by resettings it's status and updating buffers if the
 * trasnfer type is an interrupt-transfer that uses circularbuffers. */
extern void
OHCITDRestart(
    _In_ OhciController_t*          controller,
    _In_ UsbManagerTransfer_t*      transfer,
    _In_ OhciTransferDescriptor_t*  td);

/******************************************************************************
 * Isochronous Transfer Descriptor Methods
 *******************************************************************************/

/* OHCITDIsochronous
 * Creates a new isoc token td and initializes all the members.
 * The Td is immediately ready for execution. */
extern void
OHCITDIsochronous(
    _In_ OhciIsocTransferDescriptor_t* td,
    _In_ size_t                        maxPacketSize,
    _In_ uint32_t                      pid,
    _In_ struct TransferElement*       element);

/* OHCIITDDump
 * Dumps the information contained in the descriptor by writing it. */
extern void
OHCIITDDump(
    _In_ OhciController_t*              Controller,
    _In_ OhciIsocTransferDescriptor_t*  Td);

/* OHCIITDVerify
 * Checks the transfer descriptors for errors and updates the transfer that is attached
 * with the bytes transferred and error status. */
extern void
OHCIITDVerify(
        _In_ struct HCIProcessReasonScanContext* scanContext,
        _In_ OhciIsocTransferDescriptor_t*       iTD);

/* OHCIITDRestart
 * Restarts a transfer descriptor by resettings it's status and updating buffers if the
 * trasnfer type is an interrupt-transfer that uses circularbuffers. */
extern void
OHCIITDRestart(
    _In_ OhciController_t*              controller,
    _In_ UsbManagerTransfer_t*          transfer,
    _In_ OhciIsocTransferDescriptor_t*  iTD);

/*******************************************************************************
 * Queue Methods
 *******************************************************************************/

/* OHCIReloadAsynchronous
 * Reloads the control and bulk lists with new transactions that
 * are waiting in queue for execution. */
extern void
OHCIReloadAsynchronous(
        _In_ OhciController_t*    controller,
        _In_ enum USBTransferType transferType);

/* OHCIErrorCodeToTransferStatus
 * Retrieves a status-code from a given condition code */
extern enum USBTransferCode
OHCIErrorCodeToTransferStatus(
    _In_ int conditionCode);

#endif // !__USB_OHCI__

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
#include "../common/scheduler/scheduler.h"
#include "../common/hci.h"

// Controller limits.
#define OHCI_MAXPORTS                   15
#define OHCI_FRAMELIST_SIZE             32

#define OHCI_LINK_HALTED                0x1

/**
 * @brief Endpoint descriptor with links to the next endpoint and its TD queue.
 * Must be aligned to 16 bytes. The controller processes TDs before moving to
 * the next endpoint, advancing Current until it reaches EndPointer.
 */
PACKED_TYPESTRUCT(OhciQueueHead, {
    // Hardware fields (16 bytes).
    reg32_t                 Flags;
    reg32_t                 EndPointer;  // Lower 4 bits not used
    reg32_t                 Current;     // (Physical) Bit 0 - Halted, Bit 1 - Carry
    reg32_t                 LinkPointer; // Next EP (Physical)
    
    // Scheduler data.
    UsbSchedulerObject_t    Object;
});

/**
 * OhciQueueHead::Flags
 * Bits 0-6: USB device address.
 * Bits 7-10: Endpoint number.
 * Bits 11-12: Direction. 01 = OUT, 10 = IN. Other values use the TD direction.
 * Bit 13: Speed. 0 = full speed, 1 = low speed.
 * Bit 14: Skip this TD queue and move to the next endpoint.
 * Bit 15: Format. 0 = bulk/control/interrupt, 1 = isochronous.
 * Bits 16-26: Maximum packet size.
 * Bits 27-31: Type. 0000 = control, 0001 = bulk, 0010 = interrupt, 0011 = isoc.
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

/**
 * @brief Transfer descriptor with buffer addresses and saved transfer state.
 * Must be aligned to 16 bytes.
 */
PACKED_TYPESTRUCT(OhciTransferDescriptor, {
    reg32_t Flags;
    reg32_t Cbp;
    reg32_t Link;
    reg32_t BufferEnd;

    // Scheduler data and saved fields.
    UsbSchedulerObject_t Object;
    reg32_t              OriginalFlags;      // Copy of flags
    reg32_t              OriginalCbp;        // Copy of buffer
});

/**
 * OhciTransferDescriptor::Flags & OhciIsocTransferDescriptor::Flags
 * Bits 0-17: Available.
 * Bit 18: If clear, received data must exactly fill the buffer.
 * Bits 19-20: Direction. 00 = SETUP, 01 = OUT, 10 = IN.
 * Bits 21-23: Number of frames to delay the interrupt after TD completion.
 * Bits 24-25: Data toggle. Updated after each successful transmission.
 * Bits 26-27: Error count. Updated on failure, cleared on success.
 * Bits 28-31: Condition code. Records the error after a third failed attempt.
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

/**
 * @brief Isochronous descriptor with packet offsets and saved transfer state.
 * Must be aligned to 32 bytes.
 */
PACKED_TYPESTRUCT(OhciIsocTransferDescriptor, {
    reg32_t  Flags;
    reg32_t  Cbp;
    reg32_t  Link;
    reg32_t  BufferEnd;
    uint16_t Offsets[8];

    // Scheduler data and saved fields.
    UsbSchedulerObject_t Object;
    reg32_t              OriginalFlags;      // Copy of flags
    reg32_t              OriginalCbp;        // Copy of buffer
    reg32_t              OriginalBufferEnd;  // Copy of buffer end
    uint16_t             OriginalOffsets[8]; // Copy of offsets
});

#define OHCI_iTD_FRAMECOUNT(n)           ((n & 0x7) << 24)

/**
 * OhciIsocTransferDescriptor::Offsets
 * Bits 0-11: Packet size for IN transfers.
 * Bit 12: Cross-page field.
 * Bits 12-15: Condition code (error code).
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

/**
 * @brief Controller communication area with interrupt roots and frame status.
 */
PACKED_ATYPESTRUCT(volatile, OhciHCCA, {
    reg32_t  InterruptTable[OHCI_FRAMELIST_SIZE]; // Points to the 32 root nodes
    uint16_t CurrentFrame;       // Current Frame Number
    uint16_t Padding;
    uint32_t HeadDone;           // Indicates which head is done
    uint8_t  Reserved[116];      // Work area of OHCI
});

/**
 * @brief Controller registers for scheduling, interrupts, and root hub ports.
 */
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

// OhciRegisters::HcRevision values.
#define OHCI_REVISION1                  0x10
#define OHCI_REVISION11                 0x11

// OhciRegisters::HcCommandStatus bits.
#define OHCI_COMMAND_RESET              (1 << 0)
#define OHCI_COMMAND_CONTROL_FILLED     (1 << 1)
#define OHCI_COMMAND_BULK_FILLED        (1 << 2)
#define OHCI_COMMAND_OWNERSHIP          (1 << 3)

// OhciRegisters::HcControl fields.
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

// OhciRegisters::HcFmInterval fields.
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

// Descriptor pool sizes, alignment, and reserved indices.
#define OHCI_QH_ALIGNMENT                   16
#define OHCI_QH_POOL                        0
#define OHCI_QH_COUNT                       50

#define OHCI_TD_ALIGNMENT                   16
#define OHCI_TD_POOL                        1
// The active TD budget excludes the reserved null TD.
#define OHCI_TD_BUDGET                      256
#define OHCI_TD_COUNT                       (OHCI_TD_BUDGET + 1)
// Limit each endpoint batch to QEMU's traversal limit of 32 TDs.
#define OHCI_TD_BATCH                       32

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

/**
 * @brief Controller state, pending queues, register mapping, and HCCA memory.
 */
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
    struct DMABuffer* HccaDMA;
    uintptr_t HccaDeviceAddress;
    OhciHCCA_t*      Hcca;

    // State information
    unsigned int       PowerOnDelayMs;
    enum OHCIPowerMode PowerMode;
} OhciController_t;

/*******************************************************************************
 * Controller Methods
 *******************************************************************************/

/**
 * @brief Resets and initializes the controller.
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
 * @brief Resets queued transfers and reinitializes queue memory.
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
extern oserr_t
OHCIQueueDestroy(
    _In_ OhciController_t* Controller);

/**
 * @brief Checks port status and reports port changes.
 * @param controller
 * @param ignorePowerOn
 * @return
 */
extern oserr_t
OHCICheckPorts(
    _In_ OhciController_t* controller,
    _In_ int               ignorePowerOn);

/**
 * @brief Changes the controller state.
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

/**
 * @brief Initializes the queue head and controller flags for the transfer.
 */
extern oserr_t
OHCIQHInitialize(
        _In_ OhciController_t*     controller,
        _In_ UsbManagerTransfer_t* transfer,
        _In_ OhciQueueHead_t*      qh);

/**
 * @brief Writes queue head fields to the debug log.
 */
extern void
OHCIQHDump(
    _In_ OhciController_t*          Controller,
    _In_ OhciQueueHead_t*           Qh);

/**
 * @brief Restores an interrupt queue head to its starting state.
 */
extern void
OHCIQHRestart(
    _In_ OhciController_t*          Controller,
    _In_ UsbManagerTransfer_t*      Transfer);

/**
 * @brief Links the queue head into the control or bulk queue selected by type.
 */
extern void
OHCIQHLink(
    _In_ OhciController_t*          controller,
    _In_ uint8_t          type,
    _In_ OhciQueueHead_t*           qh);

/*******************************************************************************
 * Transfer Descriptor Methods
 *******************************************************************************/

/**
 * @brief Initializes a SETUP TD from the transfer element.
 * The TD must already be linked, with a buffer containing the setup packet.
 */
extern void
OHCITDSetup(
    _In_ OhciTransferDescriptor_t* td,
    _In_ struct TransferElement*   element);

/**
 * @brief Initializes a data TD for execution.
 * The TD must already be linked.
 */
extern void
OHCITDData(
    _In_ OhciTransferDescriptor_t*  td,
    _In_ enum USBTransferType       type,
    _In_ uint32_t                   pid,
    _In_ struct TransferElement*    element,
    _In_ int                        toggle);

/**
 * @brief Writes transfer descriptor fields to the debug log.
 */
extern void
OHCITDDump(
    _In_ OhciController_t*          controller,
    _In_ OhciTransferDescriptor_t*  td);

/**
 * @brief Checks the TD and records transferred bytes and errors in the scan context.
 */
extern void
OHCITDVerify(
    _In_ struct HCIProcessReasonScanContext* scanContext,
    _In_ OhciTransferDescriptor_t*           td);

/**
 * @brief Resets the TD status and updates circular buffers for interrupt transfers.
 */
extern void
OHCITDRestart(
    _In_ OhciController_t*          controller,
    _In_ UsbManagerTransfer_t*      transfer,
    _In_ OhciTransferDescriptor_t*  td);

/******************************************************************************
 * Isochronous Transfer Descriptor Methods
 *******************************************************************************/

/**
 * @brief Initializes an isochronous TD for execution.
 */
extern void
OHCITDIsochronous(
    _In_ OhciIsocTransferDescriptor_t* td,
    _In_ size_t                        maxPacketSize,
    _In_ uint32_t                      pid,
    _In_ struct TransferElement*       element);

/**
 * @brief Writes isochronous descriptor fields to the debug log.
 */
extern void
OHCIITDDump(
    _In_ OhciController_t*              Controller,
    _In_ OhciIsocTransferDescriptor_t*  Td);

/**
 * @brief Checks the isochronous TD and records transferred bytes and errors.
 */
extern void
OHCIITDVerify(
        _In_ struct HCIProcessReasonScanContext* scanContext,
        _In_ OhciIsocTransferDescriptor_t*       iTD);

/**
 * @brief Restores the isochronous TD's saved fields and next descriptor link.
 */
extern void
OHCIITDRestart(
    _In_ OhciController_t*              controller,
    _In_ UsbManagerTransfer_t*          transfer,
    _In_ OhciIsocTransferDescriptor_t*  iTD);

/*******************************************************************************
 * Queue Methods
 *******************************************************************************/

/**
 * @brief Loads waiting transactions into the selected control or bulk list.
 */
extern void
OHCIReloadAsynchronous(
        _In_ OhciController_t*    controller,
        _In_ enum USBTransferType transferType);

/**
 * @brief Converts an OHCI condition code to a USB transfer status.
 */
extern enum USBTransferCode
OHCIErrorCodeToTransferStatus(
    _In_ int conditionCode);

/**
 * @brief Suspends the controller and waits for the last frame's DMA to finish.
 *
 * @param controller Controller with mapped registers.
 * @return OS_EOK once hardware no longer accesses descriptor or payload memory.
 */
extern oserr_t
OHCIStop(
    _In_ OhciController_t* controller);

#endif // !__USB_OHCI__

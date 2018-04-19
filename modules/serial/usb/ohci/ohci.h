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
 * MollenOS MCore - Open Host Controller Interface Driver
 * TODO:
 *    - Power Management
 */

#ifndef __USB_OHCI__
#define __USB_OHCI__

/* Includes
 * - Library */
#include <os/osdefs.h>
#include <os/contracts/usbhost.h>
#include <ds/collection.h>

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
    reg32_t                 BufferBase;
});

/* OhciQueueHead::Flags
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

/* OhciTransferDescriptor
 * Describes a transfer operation that can have 3 operations, either
 * SETUP, IN or OUT. It must have a pointer to the buffer of data
 * and it must have a pointer to the end of the buffer of data. 
 * Td's must be 16 byte aligned */
PACKED_TYPESTRUCT(OhciTransferDescriptor, {
    reg32_t                 Flags;
    reg32_t                 Cbp;
    reg32_t                 Link;
    reg32_t                 BufferEnd;

    // Software Metadata
    UsbSchedulerObject_t    Object;
    reg32_t                 OriginalFlags;      // Copy of flags
    reg32_t                 OriginalCbp;        // Copy of buffer
});

/* OhciTransferDescriptor::Flags & OhciIsocTransferDescriptor::Flags
 * Contains definitions and bitfield definitions for OhciTransferDescriptor::Flags
 * Bits 0-17:  Available
 * Bits 18:    If 0, Requires the data to be recieved from an endpoint to exactly fill buffer
 * Bits 19-20: Direction, 00 = Setup (to ep), 01 = OUT (to ep), 10 = IN (from ep)
 * Bits 21-23: Interrupt delay count for this TD. This means the HC can delay interrupt a specific amount of frames after TD completion.
 * Bits 24-25: Data Toggle. It is updated after each successful transmission.
 * Bits 26-27: Error Count. Updated each transmission that fails. It is 0 on success.
 * Bits 28-31: Condition Code, if error count is 2 and it fails a third time, this contains error code. */
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

/* OhciIsocTransferDescriptor
 * Describes an isoc transfer operation that can have 2 operations, either
 * IN or OUT. It must have a pointer to the buffer of data
 * and it must have a pointer to the end of the buffer of data. 
 * iTd's must be 32 byte aligned */
PACKED_TYPESTRUCT(OhciIsocTransferDescriptor, {
    reg32_t                 Flags;
    reg32_t                 Cbp;
    reg32_t                 Link;
    reg32_t                 BufferEnd;
    uint16_t                Offsets[8];

    // Software Metadata
    UsbSchedulerObject_t    Object;
    reg32_t                 OriginalFlags;      // Copy of flags
    reg32_t                 OriginalCbp;        // Copy of buffer
    reg32_t                 OriginalBufferEnd;  // Copy of buffer end
    uint16_t                OriginalOffsets[8]; // Copy of offsets
});

#define OHCI_iTD_FRAMECOUNT(n)           ((n & 0x7) << 24)

/* OhciTransferDescriptor::Offsets
 * Contains definitions and bitfield definitions for OhciTransferDescriptor::Offsets
 * Bits 0-11:    Packet Size on IN-transmissions
 * Bits 12:      CrossPage Field
 * Bits 12-15:   Condition Code (Error Code) */
#define OHCI_iTD_OFFSETLENGTH(Length)    (Length & 0xFFF)
#define OHCI_iTD_CROSSPAGE               (1 << 12)
#define OHCI_iTD_OFFSETCODE(Flags)       ((Flags >> 12) & 0xF)

/* OhciHCCA
 * Host Controller Communcations Area.
 * This is a transfer area, where we can setup the interrupt-table
 * and where the HC will update us. The structure must be on a 256 byte boundary */
PACKED_ATYPESTRUCT(volatile, OhciHCCA, {
    reg32_t                     InterruptTable[OHCI_FRAMELIST_SIZE]; // Points to the 32 root nodes
    uint16_t                    CurrentFrame;       // Current Frame Number
    uint16_t                    Padding;
    uint32_t                    HeadDone;           // Indicates which head is done
    uint8_t                     Reserved[116];      // Work area of OHCI
});

/* OhciRegisters
 * Contains all the registers that are present in the memory mapped region
 * of the OHCI controller. The address of this structure is found in the PCI space */
PACKED_ATYPESTRUCT(volatile, OhciRegisters, {
    reg32_t                    HcRevision;
    reg32_t                    HcControl;
    reg32_t                    HcCommandStatus;
    reg32_t                    HcInterruptStatus;
    reg32_t                    HcInterruptEnable;
    reg32_t                    HcInterruptDisable;

    reg32_t                    HcHCCA;
    reg32_t                    HcPeriodCurrentED;
    reg32_t                    HcControlHeadED;
    reg32_t                    HcControlCurrentED;
    reg32_t                    HcBulkHeadED;
    reg32_t                    HcBulkCurrentED;
    reg32_t                    HcDoneHead;

    reg32_t                    HcFmInterval;
    reg32_t                    HcFmRemaining;
    reg32_t                    HcFmNumber;
    reg32_t                    HcPeriodicStart;
    reg32_t                    HcLSThreshold;

    reg32_t                    HcRhDescriptorA;
    reg32_t                    HcRhDescriptorB;
    reg32_t                    HcRhStatus;
    reg32_t                    HcRhPortStatus[OHCI_MAXPORTS];
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

/* OhciPowerMode
 * The available power-modes that an ohci controller can have. */
typedef enum _OhciPowerMode {
    AlwaysOn,
    PortControl,
    GlobalControl
} OhciPowerMode_t;

/* OhciController 
 * Contains all per-controller information that is
 * needed to control, queue and handle devices on an ohci-controller. */
typedef struct _OhciController {
    UsbManagerController_t  Base;

    // Transactions
    int                     TransactionsWaitingControl;
    int                     TransactionsWaitingBulk;

    uint16_t                TransactionQueueControlIndex;
    uint16_t                TransactionQueueBulkIndex;

    // Registers and resources
    OhciRegisters_t*        Registers;
    OhciHCCA_t*             Hcca;
    reg32_t                 HccaPhysical;

    // State information
    size_t                  PowerOnDelayMs;
    OhciPowerMode_t         PowerMode;
} OhciController_t;

/*******************************************************************************
 * Controller Methods
 *******************************************************************************/

/* OhciReset
 * Resets the controller back to usable state, does not restart the controller. */
__EXTERN
OsStatus_t
OhciReset(
    _In_ OhciController_t*  Controller);

/* OhciQueueInitialize
 * Initialize the controller's queue resources and resets counters */
__EXTERN
OsStatus_t
OhciQueueInitialize(
    _In_ OhciController_t*  Controller);
    
/* OhciQueueReset
 * Removes and cleans up any existing transfers, then reinitializes. */
__EXTERN
OsStatus_t
OhciQueueReset(
    _In_ OhciController_t*  Controller);

/* OhciQueueDestroy
 * Unschedules any scheduled ed's and frees all resources allocated
 * by the initialize function */
__EXTERN
OsStatus_t
OhciQueueDestroy(
    _In_ OhciController_t*  Controller);

/* OhciPortPrepare
 * Resets the port and also clears out any event on the port line. */
__EXTERN
OsStatus_t
OhciPortPrepare(
    _In_ OhciController_t*  Controller, 
    _In_ int                Index);

/* OhciPortInitialize
 * Initializes a port when the port has changed it's connection
 * state, no allocations are done here */
__EXTERN
OsStatus_t
OhciPortInitialize(
    _In_ OhciController_t*  Controller,
    _In_ int                Index);

/* OhciPortsCheck
 * Enumerates all the ports and detects for connection/error events */
__EXTERN
OsStatus_t
OhciPortsCheck(
    _In_ OhciController_t*  Controller);

/* OhciSetMode
 * Changes the state of the OHCI controller to the given mode */
__EXTERN
void
OhciSetMode(
    _In_ OhciController_t*  Controller, 
    _In_ reg32_t            Mode);

/*******************************************************************************
 * Queue Head Methods
 *******************************************************************************/

/* OhciQhInitialize
 * Initializes the queue head data-structure and the associated
 * hcd flags. Afterwards the queue head is ready for use. */
__EXTERN
OsStatus_t
OhciQhInitialize(
    _In_ OhciController_t*      Controller,
    _In_ UsbManagerTransfer_t*  Transfer,
    _In_ size_t                 Address,
    _In_ size_t                 Endpoint);

/* OhciQhDump
 * Dumps the information contained in the queue-head by writing it to stdout */
__EXTERN
void
OhciQhDump(
    _In_ OhciController_t*          Controller,
    _In_ OhciQueueHead_t*           Qh);

/* OhciQhRestart
 * Restarts an interrupt QH by resetting it to it's start state */
__EXTERN
void
OhciQhRestart(
    _In_ OhciController_t*          Controller,
    _In_ UsbManagerTransfer_t*      Transfer);

/* OhciQhLink 
 * Link a given queue head into the correct queue determined by Qh->Queue.
 * This can handle linkage of async and interrupt transfers. */
__EXTERN
void
OhciQhLink(
    _In_ OhciController_t*          Controller,
    _In_ UsbTransferType_t          Type,
    _In_ OhciQueueHead_t*           Qh);

/*******************************************************************************
 * Transfer Descriptor Methods
 *******************************************************************************/

/* OhciTdSetup 
 * Creates a new setup token td and initializes all the members.
 * The Td is immediately ready for execution. */
__EXTERN
void
OhciTdSetup(
    _In_ UsbTransaction_t*          Transaction,
    _In_ OhciTransferDescriptor_t*  Td);

/* OhciTdIo 
 * Creates a new io token td and initializes all the members.
 * The Td is immediately ready for execution. */
__EXTERN
void
OhciTdIo(
    _In_ OhciTransferDescriptor_t*  Td,
    _In_ UsbTransferType_t          Type,
    _In_ uint32_t                   PId,
    _In_ int                        Toggle,
    _In_ uintptr_t                  Address,
    _In_ size_t                     Length);

/* OhciTdDump
 * Dumps the information contained in the descriptor by writing it. */
__EXTERN
void
OhciTdDump(
    _In_ OhciController_t*          Controller,
    _In_ OhciTransferDescriptor_t*  Td);

/* OhciTdValidate
 * Checks the transfer descriptors for errors and updates the transfer that is attached
 * with the bytes transferred and error status. */
__EXTERN
void
OhciTdValidate(
    _In_  UsbManagerTransfer_t*     Transfer,
    _In_  OhciTransferDescriptor_t* Td);

/* OhciTdSynchronize
 * Synchronizes the toggle status of the transfer descriptor by retrieving
 * current and updating the pipe toggle. */
__EXTERN
void
OhciTdSynchronize(
    _In_  UsbManagerTransfer_t*     Transfer,
    _In_  OhciTransferDescriptor_t* Td);

/* OhciTdRestart
 * Restarts a transfer descriptor by resettings it's status and updating buffers if the
 * trasnfer type is an interrupt-transfer that uses circularbuffers. */
__EXTERN
void
OhciTdRestart(
    _In_  UsbManagerTransfer_t*     Transfer,
    _In_  OhciTransferDescriptor_t* Td);

/*******************************************************************************
 * Isochronous Transfer Descriptor Methods
 *******************************************************************************/

/* OhciTdIsochronous
 * Creates a new isoc token td and initializes all the members.
 * The Td is immediately ready for execution. */
__EXTERN
void
OhciTdIsochronous(
    _In_ OhciIsocTransferDescriptor_t*  Td,
    _In_ size_t                         MaxPacketSize,
    _In_ uint32_t                       PId,
    _In_ uintptr_t                      Address,
    _In_ size_t                         Length);

/* OhciiTdDump
 * Dumps the information contained in the descriptor by writing it. */
__EXTERN
void
OhciiTdDump(
    _In_ OhciController_t*              Controller,
    _In_ OhciIsocTransferDescriptor_t*  Td);

/* OhciiTdValidate
 * Checks the transfer descriptors for errors and updates the transfer that is attached
 * with the bytes transferred and error status. */
__EXTERN
void
OhciiTdValidate(
    _In_  UsbManagerTransfer_t*         Transfer,
    _In_  OhciIsocTransferDescriptor_t* Td);

/* OhciiTdRestart
 * Restarts a transfer descriptor by resettings it's status and updating buffers if the
 * trasnfer type is an interrupt-transfer that uses circularbuffers. */
__EXTERN
void
OhciiTdRestart(
    _In_  UsbManagerTransfer_t*         Transfer,
    _In_  OhciIsocTransferDescriptor_t* Td);

/*******************************************************************************
 * Queue Methods
 *******************************************************************************/

/* OhciReloadAsynchronous
 * Reloads the control and bulk lists with new transactions that
 * are waiting in queue for execution. */
__EXTERN
void
OhciReloadAsynchronous(
    _In_ OhciController_t*          Controller, 
    _In_ UsbTransferType_t          TransferType);

/* OhciTransactionDispatch
 * Queues the transfer up in the controller hardware, after finalizing the
 * transactions and preparing them. */
__EXTERN
UsbTransferStatus_t
OhciTransactionDispatch(
    _In_ OhciController_t*      Controller,
    _In_ UsbManagerTransfer_t*  Transfer);

/* OhciGetStatusCode
 * Retrieves a status-code from a given condition code */
__EXTERN
UsbTransferStatus_t
OhciGetStatusCode(
    _In_ int                ConditionCode);

#endif // !__USB_OHCI__

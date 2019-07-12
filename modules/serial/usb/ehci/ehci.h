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
 */

#ifndef __USB_EHCI__
#define __USB_EHCI__

#include <os/osdefs.h>
#include <ddk/contracts/usbhost.h>
#include <ds/collection.h>
#include <limits.h>

#include "../common/manager.h"
#include "../common/scheduler.h"
#include "../common/hci.h"

/* EHCI Controller Definitions 
 * Contains generic magic constants and definitions */
#define EHCI_MAX_PORTS              15
#define EHCI_MAX_BANDWIDTH          800
#define EHCI_VERSION_10             0x0100
#define EHCI_VERSION_11             0x0110

PACKED_ATYPESTRUCT(volatile, EchiCapabilityRegisters, {
    uint8_t                     Length;
    uint8_t                     Reserved;
    uint16_t                    Version;
    reg32_t                     SParams;
    reg32_t                     CParams;
    reg64_t                     PortRouting;
});

/* EchiCapabilityRegisters::SParams
 * Contains definitions and bitfield definitions for EchiCapabilityRegisters::SParams
 * Bits 0-3: Port Count (max of 15 ports) 
 * Bits 4: Port Power Control (If set, ports have power switches) 
 * Bits 5-6: Reserved
 * Bits 7: Port Routing Rules. 
 * Bits 8-11: Number of ports per companion controller 
 * Bits 12-15: Number of companion controllers 
 * Bits 16: Port Indicators Support 
 * Bits 17-19: Reserved
 * Bits 20-23: Debug Port Number 
 * Bits 24-31: Reserved */
#define EHCI_SPARAM_PORTCOUNT(n)            (n & 0xF)
#define EHCI_SPARAM_PPC                     (1 << 4)
#define EHCI_SPARAM_PORTROUTING             (1 << 7)
#define EHCI_SPARAM_CCPCOUNT(n)             ((n >> 8) & 0xF)
#define EHCI_SPARAM_CCCOUNT(n)              ((n >> 12) & 0xF)
#define EHCI_SPARAM_PORTINDICATORS          (1 << 16)
#define EHCI_SPARAM_DEBUGPORT(n)            ((n >> 20) & 0xF)

/* EchiCapabilityRegisters::CParams
 * Contains definitions and bitfield definitions for EchiCapabilityRegisters::CParams
 * Bits 0: 64 Bit Capability if set (Use 64 bit data structures instead of 32 bit)! 
 * Bits 1: Frame List Flag, if set we can control how long a frame list is, otherwise it is 1024.
 * Bits 2: Asynchronous Schedule Park Support, if set it supports park feature for high-speed queue heads. 
 * Bits 3: Reserved
 * Bits 4-7: Isochronous Scheduling Threshold
 * Bits 8-15: Extended Capability Pointer, EECP. If a value is above 0x40 its a valid offset into pci-space. 
 * Bits 16: Hardware Prefect Capability
 * Bits 17: Link Power Management Capability
 * Bits 18: Per-Port Change Event Capability
 * Bits 19: 32-Frame Capability
 * Bits 20-31: Reserved */
#define EHCI_CPARAM_64BIT                   (1 << 0)
#define EHCI_CPARAM_VARIABLEFRAMELIST       (1 << 1)
#define EHCI_CPARAM_ASYNCPARK               (1 << 2)
#define EHCI_CPARAM_ISOCTHRESHOLD(n)        ((n >> 4) & 0xF)
#define EHCI_CPARAM_EECP(n)                 ((n >> 8) & 0xFF)
#define EHCI_CPARAM_HWPREFETCH              (1 << 16)
#define EHCI_CPARAM_LINK_PWR                (1 << 17)
#define EHCI_CPARAM_PERPORT_CHANGE          (1 << 18)
#define EHCI_CPARAM_32FRAME_SUPPORT         (1 << 19)

/* EchiOperationalRegisters
 * Registers that are used to control and command the EHCI controller
 * and its ports. */
PACKED_ATYPESTRUCT(volatile, EchiOperationalRegisters, {
    reg32_t                        UsbCommand;
    reg32_t                        UsbStatus;
    reg32_t                        UsbIntr;
    reg32_t                        FrameIndex;
    reg32_t                        SegmentSelector;        // 4G Segment Selector
    reg32_t                        PeriodicListAddress;    // Periodic List
    reg32_t                        AsyncListAddress;        // AsyncList
    uint8_t                        Reserved[(0x40 - 0x1C)];
    reg32_t                        ConfigFlag;
    reg32_t                        Ports[EHCI_MAX_PORTS];
});

/* EchiOperationalRegisters::UsbCommand
 * Contains definitions and bitfield definitions for EchiOperationalRegisters::UsbCommand */
#define EHCI_COMMAND_RUN                (1 << 0)
#define EHCI_COMMAND_HCRESET            (1 << 1)
#define EHCI_COMMAND_LISTSIZE(n)        ((n & 0x3) << 2)
#define EHCI_COMMAND_PERIODIC_ENABLE    (1 << 4)
#define EHCI_COMMAND_ASYNC_ENABLE       (1 << 5)
#define EHCI_COMMAND_IOC_ASYNC_DOORBELL (1 << 6)
#define EHCI_COMMAND_LIGHT_HCRESET      (1 << 7)
#define EHCI_COMMAND_PARK_COUNT(n)      ((n & 0x3) << 8)
#define EHCI_COMMAND_ASYNC_PARKMODE     (1 << 11)
#define EHCI_COMMAND_PERIOD_PREFECTCH   (1 << 12)
#define EHCI_COMMAND_ASYNC_PREFETCH     (1 << 13)
#define EHCI_COMMAND_FULL_PREFETCH      (1 << 14)
#define EHCI_COMMAND_PERPORT_ENABLE     (1 << 15)
#define EHCI_COMMAND_INTR_THRESHOLD(n)  ((n & 0xFF) << 16)
#define EHCI_COMMAND_HI_RESUME(n)       ((n & 0xF) << 24)

#define EHCI_LISTSIZE_1024              0
#define EHCI_LISTSIZE_512               1
#define EHCI_LISTSIZE_256               2
#define EHCI_LISTSIZE_32                3

/* EchiOperationalRegisters::UsbStatus
 * Contains definitions and bitfield definitions for EchiOperationalRegisters::UsbStatus */
#define EHCI_STATUS_PROCESS             (1 << 0)
#define EHCI_STATUS_PROCESSERROR        (1 << 1)
#define EHCI_STATUS_PORTCHANGE          (1 << 2)
#define EHCI_STATUS_FLROLLOVER          (1 << 3)
#define EHCI_STATUS_HOSTERROR           (1 << 4)
#define EHCI_STATUS_ASYNC_DOORBELL      (1 << 5)

#define EHCI_STATUS_HALTED              (1 << 12)
#define EHCI_STATUS_RECLAMATION         (1 << 13)
#define EHCI_STATUS_PERIODIC_ACTIVE     (1 << 14)
#define EHCI_STATUS_ASYNC_ACTIVE        (1 << 15)
#define EHCI_STATUS_PERPORTCHANGE(n)    (1 << (16 + n))

/* EchiOperationalRegisters::UsbIntr
 * Contains definitions and bitfield definitions for EchiOperationalRegisters::UsbIntr */
#define EHCI_INTR_PROCESS               (1 << 0)
#define EHCI_INTR_PROCESSERROR          (1 << 1)
#define EHCI_INTR_PORTCHANGE            (1 << 2)
#define EHCI_INTR_FLROLLOVER            (1 << 3)
#define EHCI_INTR_HOSTERROR             (1 << 4)
#define EHCI_INTR_ASYNC_DOORBELL        (1 << 5)
#define EHCI_INTR_PERPORTCHANGE(n)      (1 << (16 + n))

/* EchiOperationalRegisters::Ports[n]
 * Contains definitions and bitfield definitions for EchiOperationalRegisters::Ports[n] */
#define EHCI_PORT_CONNECTED             (1 << 0)
#define EHCI_PORT_CONNECT_EVENT         (1 << 1)
#define EHCI_PORT_ENABLED               (1 << 2)
#define EHCI_PORT_ENABLE_EVENT          (1 << 3)
#define EHCI_PORT_OVERCURRENT           (1 << 4)
#define EHCI_PORT_OC_EVENT              (1 << 5)
#define EHCI_PORT_FORCERESUME           (1 << 6)
#define EHCI_PORT_SUSPENDED             (1 << 7)
#define EHCI_PORT_RESET                 (1 << 8)
#define EHCI_PORT_L1SUSPEND             (1 << 9)

#define EHCI_PORT_LINESTATUS(n)         ((n >> 10) & 0x3)
#define EHCI_LINESTATUS_RELEASE         0x1
// If any other state is present, reset EHCI

#define EHCI_PORT_POWER                 (1 << 12)
#define EHCI_PORT_COMPANION_HC          (1 << 13)
#define EHCI_PORT_COLOR_AMBER           (1 << 14)
#define EHCI_PORT_COLOR_GREEN           (1 << 15)

// Wake bits and RWC
#define EHCI_PORT_CONNECT_WAKE          (1 << 20)
#define EHCI_PORT_DISCONNECT_WAKE       (1 << 21)
#define EHCI_PORT_OC_WAKE               (1 << 22)
#define EHCI_PORT_RWC                   (EHCI_PORT_CONNECT_EVENT | EHCI_PORT_ENABLE_EVENT | EHCI_PORT_OC_EVENT)

/* EhciLink::LinkBits
 * Contains definitions and bitfield definitions for EhciLink::LinkBits
 * Bit 0: Terminate
 * Bit 1-2: Type
 * Bit 3-4: 0 */
#define EHCI_LINK_END                   (1 << 0)
#define EHCI_LINK_iTD                   0
#define EHCI_LINK_QH                    (1 << 1)
#define EHCI_LINK_siTD                  (2 << 1)
#define EHCI_LINK_FSTN                  (3 << 1)
#define EHCI_LINK_TYPE(n)               ((n >> 1) & 0x3)

/* EhciIsochronousDescriptor
 * A generic isochronous transfer descripter specifcally for high-speed
 * devices. This is not used with queue heads, is its own queue head. 
 * Must be 32 byte aligned */
PACKED_TYPESTRUCT(EhciIsochronousDescriptor, {
    reg32_t                 Link;
    reg32_t                 Transactions[8];
    reg32_t                 Buffers[7];
    reg32_t                 ExtBuffers[7]; // 92 bytes for all this

    // Software metadata
    UsbSchedulerObject_t    Object;
    uint16_t                QueueIndex;
    reg32_t                 TransactionsCopy[8];
});

/* EhciIsochronousDescriptor::Transactions[0-7]
 * Contains definitions and bitfield definitions for EhciIsochronousDescriptor::Transaction[0-7]
 * Bit 0-11: Transaction Offset
 * Bit 12-14: Page Selector 
 * Bit 15: Interrupt on Completion 
 * Bit 16-27: Transaction Length 
 * Bit 28-31: Condition Code (Status) */
#define EHCI_iTD_OFFSET(n)              (n & 0xFFF)
#define EHCI_iTD_PAGE(n)                ((n & 0x7) << 12)
#define EHCI_iTD_IOC                    (1 << 15)
#define EHCI_iTD_LENGTH(n)              ((n & 0xFFF) << 16)
#define EHCI_iTD_ACTIVE                 (1 << 31)

#define EHCI_iTD_CC(n)                  ((n >> 28) & 0xF)

/* EhciIsochronousDescriptor::Buffers[0]
 * Contains definitions and bitfield definitions for EhciIsochronousDescriptor::Buffers[0]
 * Bit 0-6: Device Address 
 * Bit 7: Reserved 
 * Bit 8-11: Endpoint Number 
 * Bit 12-31: Buffer Page */
#define EHCI_iTD_DEVADDR(n)             (n & 0x7F)
#define EHCI_iTD_EPADDR(n)              ((n & 0xF) << 8)

/* EhciIsochronousDescriptor::Buffers[1]
 * Contains definitions and bitfield definitions for EhciIsochronousDescriptor::Buffers[1]
 * Bit 0-10: Maximum Packet Size (max 400h)
 * Bit 11: Direction (0 = out, 1 = in) 
 * Bit 12-31: Buffer Page */
#define EHCI_iTD_MPS(n)                 (MIN(1024, n))
#define EHCI_iTD_IN                     (1 << 11);
#define EHCI_iTD_OUT                    0

/* EhciIsochronousDescriptor::Buffers[2]
 * Contains definitions and bitfield definitions for EhciIsochronousDescriptor::Buffers[2]
 * Bit 0-1: How many transactions per micro-frame should be executed
 * Bit 12-31: Buffer Page */
#define EHCI_iTD_TRANSACTIONCOUNT(n)    (n & 0x3)

/* EhciIsochronousDescriptor::Buffers[3-6]
 * Contains definitions and bitfield definitions for EhciIsochronousDescriptor::Buffers[3-6]
 * Bit 0-11: Reserved
 * Bit 12-31: Buffer Page */
#define EHCI_iTD_BUFFER(n)              (n & 0xFFFFF000)
#define EHCI_iTD_EXTBUFFER(n)           ((n >> 32) & 0xFFFFFFFF)

/* EhciSplitIsochronousDescriptor
 * All Full-speed isochronous transfers through Transaction Translators 
 * are managed using the siTD data structure. Must be 32 byte aligned */
PACKED_TYPESTRUCT(EhciSplitIsochronousDescriptor, {
    reg32_t                 Link;
    reg32_t                 Flags;
    uint8_t                 FrameStartMask;
    uint8_t                 FrameCompletionMask;
    uint16_t                Reserved;
    reg32_t                 Status;
    reg32_t                 Bp0AndOffset;
    reg32_t                 Bp1AndInfo;
    reg32_t                 BackPointer;
    reg32_t                 ExtBp0;
    reg32_t                 ExtBp1;

    // Software metadata
    UsbSchedulerObject_t    Object;
});

/* EhciSplitIsochronousDescriptor::Flags
 * Contains definitions and bitfield definitions for EhciSplitIsochronousDescriptor::Flags
 * Bit 0-11: Transaction Offset
 * Bit 12-14: Page Selector 
 * Bit 15: Interrupt on Completion 
 * Bit 16-27: Transaction Length 
 * Bit 28-31: Condition Code (Status) */
#define EHCI_siTD_XACTIONOFFSET(Offset) (Offset & 0xFFF)
#define EHCI_siTD_PAGESELECT(Page)      ((Page & 0x7) << 12)
#define EHCI_siTD_IOC                   (1 << 15)
#define EHCI_siTD_LENGTH(Length)        ((Length & 0xFFF) << 16)
#define EHCI_siTD_CC(Code)              ((Code & 0xF) << 28)

/* EhciSplitIsochronousDescriptor::Status
 * Contains definitions and bitfield definitions for EhciSplitIsochronousDescriptor::Status
 * Bit 0-7: Status
 * Bit 8-15: Frame Complete-split Progress Mask 
 * Bit 16-25: Transfer Length (max 3FFh)
 * Bit 26-29: Reserved
 * Bit 30: Page Select (0 = Bp0, 1 = Bp1)
 * Bit 31: IOC */
#define EHCI_siTD_ACTIVE                (1 << 7)
#define EHCI_siTD_STATUS(n)             (n & 0xFF)
#define EHCI_siTD_CSPMASK(n)            ((n & 0xFF) << 8)
#define EHCI_siTD_XFERLENGTH(n)         ((n & 0x3FF) << 16)
#define EHCI_siTD_PAGE(n)               ((n & 0x1) << 30)
#define EHCI_siTD_IOCSTATUS             (n << 31)

#define EHCI_siTD_DEVADDR(n)            (n & 0x7F)
#define EHCI_siTD_EPADDR(n)             ((n & 0xF) << 8)
#define EHCI_siTD_HUBADDR(n)            ((n & 0x7F) << 16)
#define EHCI_siTD_PORT(n)               ((n & 0x7F) << 24)
#define EHCI_siTD_OUT                   0
#define EHCI_siTD_IN                    (1 << 31)

#define EHCI_siTD_SMASK(n)              (n & 0xFF)
#define EHCI_siTD_CMASK(n)              ((n & 0xFF) << 8)

/* EhciSplitIsochronousDescriptor::Bp0AndOffset
 * Contains definitions and bitfield definitions for EhciSplitIsochronousDescriptor::Bp0AndOffset
 * Bit 0-11: Current Offset 
 * Bit 12-31: Bp0 */
#define EHCI_siTD_OFFSET(n)             (n & 0xFFF)
#define EHCI_siTD_BUFFER(n)             (n & 0xFFFFF000)

/* EhciSplitIsochronousDescriptor::Bp1AndInfo
 * Contains definitions and bitfield definitions for EhciSplitIsochronousDescriptor::Bp1AndInfo
 * Bit 0-2: Transaction count, max val 6
 * Bit 3-4: Transaction Position, 
 * Bit 5-11: Reserved
 * Bit 12-31: Bp1 */
#define EHCI_siTD_TCOUNT(n)             (MIN(6, n) & 0x7)
#define EHCI_siTD_POSITION_ALL          0
#define EHCI_siTD_POSITION_BEGIN        (1 << 3)
#define EHCI_siTD_POSITION_MID          (2 << 3)
#define EHCI_siTD_POSITION_END          (3 << 3)

/* EhciTransferDescriptor
 * Generic transfer descriptor, used for bulk and control transactions.
 * 52 Bytes are used by hardware - structure must be 32 byte aligned. */
PACKED_TYPESTRUCT(EhciTransferDescriptor, {
    reg32_t                 Link;
    reg32_t                 AlternativeLink; // Used if short-packet is detected
    uint8_t                 Status;
    uint8_t                 Token;
    uint16_t                Length;
    reg32_t                 Buffers[5];
    reg32_t                 ExtBuffers[5];

    // Software metadata
    UsbSchedulerObject_t    Object;
    uint8_t                 OriginalToken;
    uint16_t                OriginalLength;
});

/* EhciTransferDescriptor::Status
 * Contains definitions and bitfield definitions for EhciTransferDescriptor::Status */
#define EHCI_TD_PINGSTATE           (1 << 0)
#define EHCI_TD_SPLITXACT           (1 << 1)
#define EHCI_TD_INCOMPLETE          (1 << 2)
#define EHCI_TD_XACT                (1 << 3) // CRC/Timeout, Bad PID
#define EHCI_TD_BABBLE              (1 << 4)
#define EHCI_TD_DATABUFERROR        (1 << 5)
#define EHCI_TD_HALTED              (1 << 6)
#define EHCI_TD_ACTIVE              (1 << 7)

/* EhciTransferDescriptor::Token
 * Contains definitions and bitfield definitions for EhciTransferDescriptor::Token
 * Bit 0-1: PID
 * Bit 2-3: Error Count
 * Bit 4-6: Page Selector (0-4 value)
 * Bit 7: IOC */
#define EHCI_TD_OUT                 0
#define EHCI_TD_IN                  (1 << 0)
#define EHCI_TD_SETUP               (1 << 1)
#define EHCI_TD_ERRCOUNT            (3 << 2)
#define EHCI_TD_PAGE(n)             (MIN(4, n) << 4)
#define EHCI_TD_IOC                 (1 << 7)

/* EhciTransferDescriptor::Length
 * Contains definitions and bitfield definitions for EhciTransferDescriptor::Length
 * Bit 0-14: Length (Max Value 0x5000 (5 Pages))
 * Bit 15: Data Toggle */
#define EHCI_TD_LENGTHMASK          0x7FFF
#define EHCI_TD_LENGTH(n)           (MIN(20480, n))
#define EHCI_TD_TOGGLE              (1 << 15)

/* EhciTransferDescriptor::Buffers[5]
 * Contains definitions and bitfield definitions for EhciTransferDescriptor::Buffers[5]
 * Bit 0-11: Current Offset
 * Bit 12-31: Buffer Page */
#define EHCI_TD_OFFSET(n)           (n & 0xFFF)
#define EHCI_TD_BUFFER(n)           (n & 0xFFFFF000)
#define EHCI_TD_EXTBUFFER(n)        (((uint64_t)n >> 32) & 0xFFFFFFFF)

#define EHCI_TD_CERR(n)             ((n >> 10) & 0x3)
#define EHCI_TD_CC(n)               (n & 0xFF)

/* EhciQueueHeadOverlay
 * This is the TD work area, most of the members are equal to the definitions
 * presented in the generic transfer descriptor. We only describe them if they
 * differ. */
PACKED_TYPESTRUCT(EhciQueueHeadOverlay, {
    reg32_t                     NextTD;
    reg32_t                     NextAlternativeTD;
    uint8_t                     Status;
    uint8_t                     Token;
    uint16_t                    Length;
    reg32_t                     Buffers[5];
    reg32_t                     ExtBuffers[5];
});

/* EhciQueueHeadOverlay::NextAlternativeTD
 * Contains definitions and bitfield definitions for EhciQueueHeadOverlay::NextAlternativeTD
 * Bit 0: Terminate
 * Bit 1-4: NAK Count */
#define EHCI_TD_NAKCOUNT(Val)           ((Val >> 1) & 0xF)

/* EhciQueueHeadOverlay::Buffers[1]
 * Contains definitions and bitfield definitions for EhciQueueHeadOverlay::Buffers[1]
 * Bit 0-7: Completion Process Mask
 * Bit 8-11: Reserved
 * Bit 12-31: Buffer Page */
#define EHCI_TD_CPMASK(Val)             (Val & 0xFF)

/* EhciQueueHeadOverlay::Buffers[2]
 * Contains definitions and bitfield definitions for EhciQueueHeadOverlay::Buffers[2]
 * Bit 0-4: Frame Tag
 * Bit 5-11: S-Bytes
 * Bit 12-31: Buffer Page */
#define EHCI_TD_FRAMETAG(Val)           (Val & 0x1F)
#define EHCI_TD_SBYTES(Val)             ((Val >> 5) & 0x7F)

/* EhciQueueHead
 * Generic queue head, contains a working area aswell. 
 * The structure must be 32 bytes aligned. */
PACKED_TYPESTRUCT(EhciQueueHead, {
    reg32_t                 LinkPointer;
    reg32_t                 Flags;
    uint8_t                 FrameStartMask;
    uint8_t                 FrameCompletionMask;
    uint16_t                State;
    reg32_t                 Current;
    EhciQueueHeadOverlay_t  Overlay; // Current working area, contains td

    // Software metadata
    UsbSchedulerObject_t    Object;
});

/* EhciQueueHead::Flags
 * Contains definitions and bitfield definitions for EhciQueueHead::Flags
 * Bit 0-6: Device Address
 * Bit 7: Inactivate on Next Transaction (Interrupt only)
 * Bit 8-11: Endpoint Number 
 * Bit 12-13: Endpoint Speed
 * Bit 14: Data Toggle Control
 * Bit 15: Head of Reclamation List Flag
 * Bit 16-26: Max Packet Length
 * Bit 27: Control Endpoint Flag
 * Bit 28-31: Nak Recount Load */
#define EHCI_QH_DEVADDR(n)              (n & 0x7F)
#define EHCI_QH_INVALIDATE_NEXT         (1 << 7)
#define EHCI_QH_EPADDR(n)               ((n & 0xF) << 8)
#define EHCI_QH_LOWSPEED                (1 << 12)
#define EHCI_QH_FULLSPEED               0
#define EHCI_QH_HIGHSPEED               (1 << 13)
#define EHCI_QH_DTC                     (1 << 14)
#define EHCI_QH_RECLAMATIONHEAD         (1 << 15)
#define EHCI_QH_MAXLENGTH(n)            (MIN(1024, n) << 16)
#define EHCI_QH_CONTROLEP               (1 << 27)
#define EHCI_QH_RL(n)                   ((n & 0xF) << 28)

/* EhciQueueHead::State
 * Contains definitions and bitfield definitions for EhciQueueHead::State
 * Bit 0-6: Hub Address
 * Bit 7-13: Port Number
 * Bit 14-15: Multi */
#define EHCI_QH_HUBADDR(n)              (n & 0x7F)
#define EHCI_QH_PORT(n)                 ((n & 0x7F) << 7)
#define EHCI_QH_MULTIPLIER(n)           ((n & 0x3) << 14)

/* EhciFSTN
 * This data structure is to be used only for managing Full- and Low-speed transactions that span a Host-frame
 * boundary. Software must not use an FSTN in the
 * Asynchronous Schedule. An FSTN in the Asynchronous schedule results in undefined behavior. Software
 * must not use the FSTN feature with a host controller whose HCIVERSION register indicates a revision
 * implementation below 0096h. FSTNs are not defined for implementations before 0.96 and their use will
 * yield undefined results. */
PACKED_TYPESTRUCT(EhciFSTN, {
    reg32_t                    PathPointer;     // HW Link (Next Periodic Element)
    reg32_t                    BackPathPointer; // HW Link
});

/* Ehci Pool Definitions
 * The Ehci controller must keep track of pools for each type of transfer object
 * - Queue Heads
 * - (+Split)Isochronous (High-Speed) Descriptors
 * - Queue Element Transfer Descriptors */
#define EHCI_QH_ALIGNMENT                   32
#define EHCI_QH_POOL                        0
#define EHCI_QH_COUNT                       50

#define EHCI_TD_ALIGNMENT                   32
#define EHCI_TD_POOL                        1
#define EHCI_TD_COUNT                       400

#define EHCI_iTD_ALIGNMENT                  32
#define EHCI_iTD_POOL                       2
#define EHCI_iTD_COUNT                      50

#define EHCI_siTD_ALIGNMENT                 32
#define EHCI_siTD_POOL                      3
#define EHCI_siTD_COUNT                     50

#define EHCI_QH_NULL                        0
#define EHCI_QH_ASYNC                       1
#define EHCI_QH_START                       2

#define EHCI_TD_NULL                        0
#define EHCI_TD_START                       1

#define EHCI_iTD_NULL                       0
#define EHCI_iTD_START                      1

#define EHCI_siTD_NULL                      0
#define EHCI_siTD_START                     1

/* EhciController 
 * Contains all per-controller information that is
 * needed to control, queue and handle devices on an ehci-controller. */
typedef struct _EhciController {
    UsbManagerController_t      Base;

    // Registers and resources
    EchiCapabilityRegisters_t*  CapRegisters;
    EchiOperationalRegisters_t* OpRegisters;
    size_t                      FrameCount;

    // Copy of vital registers
    reg32_t                     SParameters;
    reg32_t                     CParameters;
} EhciController_t;

/*******************************************************************************
 * Controller Methods
 *******************************************************************************/

/* EhciQueueInitialize
 * Initialize the controller's queue resources and resets counters */
__EXTERN
OsStatus_t
EhciQueueInitialize(
    _In_ EhciController_t *Controller);

/* EhciQueueDestroy
 * Unschedules any scheduled ed's and frees all resources allocated
 * by the initialize function */
__EXTERN
OsStatus_t
EhciQueueDestroy(
    _In_ EhciController_t *Controller);

/* EhciQueueReset
 * Removes and cleans up any existing transfers, then reinitializes. */
__EXTERN
OsStatus_t
EhciQueueReset(
    _In_ EhciController_t *Controller);

/* EhciEnableScheduler
 * Enables the relevant scheduler if it is not enabled already */
__EXTERN
void
EhciEnableScheduler(
    _In_ EhciController_t*  Controller,
    _In_ UsbTransferType_t  Type);

/* EhciSetPrefetching
 * Disables the prefetching related to the transfer-type. */
__EXTERN
OsStatus_t
EhciSetPrefetching(
    _In_ EhciController_t*  Controller,
    _In_ UsbTransferType_t  Type,
    _In_ int                Set);

/* EhciHalt
 * Halt's the controller and clears any pending events. */
__EXTERN
OsStatus_t
EhciHalt(
    _In_ EhciController_t *Controller);

/* EhciRestart
 * Resets and restarts the entire controller and schedule, this can be used in
 * case of serious failures. */
__EXTERN
OsStatus_t
EhciRestart(
    _In_ EhciController_t *Controller);

/* EhciPortClearBits
 * Clears the given bits without touching the R/WC bits */
__EXTERN
void
EhciPortClearBits(
    _In_ EhciController_t*          Controller,
    _In_ int                        Index,
    _In_ reg32_t                    Bits);

/* EhciPortSetBits
 * Sets the given bits without touching the R/WC bits */
__EXTERN
void
EhciPortSetBits(
    _In_ EhciController_t*          Controller,
    _In_ int                        Index,
    _In_ reg32_t                    Bits);

/* EhciPortScan
 * Scans all ports of the controller for event-changes and handles
 * them accordingly. */
__EXTERN
void
EhciPortScan(
    _In_ EhciController_t*  Controller,
    _In_ reg32_t            ChangeBits);

/*******************************************************************************
 * Queue Head Methods
 *******************************************************************************/

/* EhciQhInitialize
 * This initiates any periodic scheduling information 
 * that might be needed */
__EXTERN
OsStatus_t
EhciQhInitialize(
    _In_ EhciController_t*          Controller,
    _In_ UsbManagerTransfer_t*      Transfer,
    _In_ size_t                     Address,
    _In_ size_t                     Endpoint);

/* EhciQhDump
 * Dumps the information contained in the queue-head by writing it to stdout */
__EXTERN
void
EhciQhDump(
    _In_ EhciController_t*          Controller,
    _In_ EhciQueueHead_t*           Qh);

/* EhciQhRestart
 * Restarts an interrupt QH by resetting it to it's start state */
__EXTERN
void
EhciQhRestart(
    EhciController_t*               Controller, 
    UsbManagerTransfer_t*           Transfer);

/*******************************************************************************
 * Transfer Descriptor Methods
 *******************************************************************************/

/* EhciTdSetup
 * This allocates & initializes a TD for a setup transaction 
 * this is only used for control transactions */
__EXTERN
size_t
EhciTdSetup(
    _In_ EhciController_t*         Controller,
    _In_ EhciTransferDescriptor_t* Td,
    _In_ uintptr_t                 Address,
    _In_ size_t                    Length);

/* EhciTdIo
 * This allocates & initializes a TD for an i/o transaction 
 * and is used for control, bulk and interrupt */
__EXTERN
size_t
EhciTdIo(
    _In_ EhciController_t*         Controller,
    _In_ EhciTransferDescriptor_t* Td,
    _In_ size_t                    MaxPacketSize,
    _In_ UsbTransactionType_t      Direction,
    _In_ uintptr_t                 Address,
    _In_ size_t                    Length,
    _In_ int                       Toggle);

/* EhciTdDump
 * Dumps the information contained in the descriptor by writing it. */
__EXTERN
void
EhciTdDump(
    _In_ EhciController_t*          Controller,
    _In_ EhciTransferDescriptor_t*  Td);

/* EhciTdValidate
 * Checks the transfer descriptors for errors and updates the transfer that is attached
 * with the bytes transferred and error status. */
__EXTERN
void
EhciTdValidate(
    _In_ UsbManagerTransfer_t*      Transfer,
    _In_ EhciTransferDescriptor_t*  Td);

/* EhciTdSynchronize
 * Synchronizes the toggle status of the transfer descriptor by retrieving
 * current and updating the pipe toggle. */
__EXTERN
void
EhciTdSynchronize(
    _In_ UsbManagerTransfer_t*      Transfer,
    _In_ EhciTransferDescriptor_t*  Td);

/* EhciTdRestart
 * Restarts a transfer descriptor by resettings it's status and updating buffers if the
 * trasnfer type is an interrupt-transfer that uses circularbuffers. */
__EXTERN
void
EhciTdRestart(
    _In_ EhciController_t*          Controller,
    _In_ UsbManagerTransfer_t*      Transfer,
    _In_ EhciTransferDescriptor_t*  Td);

/*******************************************************************************
 * Isochronous TD Methods
 *******************************************************************************/

/* EhciTdIsochronous
 * This initiates any periodic scheduling information that might be needed */
__EXTERN
OsStatus_t
EhciTdIsochronous(
    _In_ EhciController_t*              Controller,
    _In_ UsbTransfer_t*                 Transfer,
    _In_ EhciIsochronousDescriptor_t*   iTd,
    _In_ uintptr_t                      BufferAddress,
    _In_ size_t                         ByteCount,
    _In_ size_t                         Address,
    _In_ size_t                         Endpoint);

/* EhciiTdDump
 * Dumps the information contained in the descriptor by writing it. */
__EXTERN
void
EhciiTdDump(
    _In_ EhciController_t*              Controller,
    _In_ EhciIsochronousDescriptor_t*   Td);

/* EhciiTdValidate
 * Checks the transfer descriptors for errors and updates the transfer that is attached
 * with the bytes transferred and error status. */
__EXTERN
void
EhciiTdValidate(
    _In_ UsbManagerTransfer_t*          Transfer,
    _In_ EhciIsochronousDescriptor_t*   Td);

/* EhciiTdRestart
 * Restarts a transfer descriptor by resettings it's status and updating buffers if the
 * trasnfer type is an interrupt-transfer that uses circularbuffers. */
__EXTERN
void
EhciiTdRestart(
    _In_ EhciController_t*              Controller,
    _In_ UsbManagerTransfer_t*          Transfer,
    _In_ EhciIsochronousDescriptor_t*   Td);

/*******************************************************************************
 * Queue Methods
 *******************************************************************************/
    
/* EhciGetStatusCode
 * Retrieves a status-code from a given condition code */
__EXTERN
UsbTransferStatus_t
EhciGetStatusCode(
    _In_ int                    ConditionCode);

/* EhciConditionCodeToIndex
 * Converts a given condition bit-index to number */
__EXTERN
int
EhciConditionCodeToIndex(
    _In_ unsigned               ConditionCode);

/* EhciRingDoorbell
 * This functions rings the bell */
__EXTERN
void
EhciRingDoorbell(
     _In_ EhciController_t*     Controller);

/* EhciTransactionDispatch
 * Queues the transfer up in the controller hardware, after finalizing the
 * transactions and preparing them. */
__EXTERN
UsbTransferStatus_t
EhciTransactionDispatch(
    _In_ EhciController_t*      Controller,
    _In_ UsbManagerTransfer_t*  Transfer);

#endif //!__USB_EHCI__

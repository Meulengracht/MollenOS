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
 */

#ifndef __USB_EHCI__
#define __USB_EHCI__

#include <os/osdefs.h>
#include <limits.h>

#include "../common/manager.h"
#include "../common/scheduler.h"
#include "../common/hci.h"

// forward declarations
typedef struct EHCIController EhciController_t;

/**
 * EHCI limits and supported versions.
 */
#define EHCI_MAX_PORTS     15
#define EHCI_MAX_BANDWIDTH 800
#define EHCI_VERSION_10    0x0100
#define EHCI_VERSION_11    0x0110

/**
 * Controller version, capabilities, and companion port routing registers.
 */
PACKED_ATYPESTRUCT(volatile, EchiCapabilityRegisters, {
    uint8_t  Length;
    uint8_t  Reserved;
    uint16_t Version;
    reg32_t  SParams;
    reg32_t  CParams;
    reg64_t  PortRouting;
});

/**
 * EchiCapabilityRegisters::SParams
 * Bits 0-3: Port Count (max of 15 ports) 
 * Bits 4: Port Power Control (If set, ports have power switches) 
 * Bits 5-6: Reserved
 * Bits 7: Port Routing Rules. 
 * Bits 8-11: Number of ports per companion controller 
 * Bits 12-15: Number of companion controllers 
 * Bits 16: Port Indicators Support 
 * Bits 17-19: Reserved
 * Bits 20-23: Debug Port Number 
 * Bits 24-31: Reserved
 */
#define EHCI_SPARAM_PORTCOUNT(n)            (n & 0xF)
#define EHCI_SPARAM_PPC                     (1 << 4)
#define EHCI_SPARAM_PORTROUTING             (1 << 7)
#define EHCI_SPARAM_CCPCOUNT(n)             ((n >> 8) & 0xF)
#define EHCI_SPARAM_CCCOUNT(n)              ((n >> 12) & 0xF)
#define EHCI_SPARAM_PORTINDICATORS          (1 << 16)
#define EHCI_SPARAM_DEBUGPORT(n)            ((n >> 20) & 0xF)

/**
 * EchiCapabilityRegisters::CParams
 * Bits 0: 64-bit capability. If set, use 64-bit data structures.
 * Bits 1: Variable frame list length. If clear, the length is 1024.
 * Bits 2: Asynchronous schedule park support for high-speed queue heads.
 * Bits 3: Reserved
 * Bits 4-7: Isochronous Scheduling Threshold
 * Bits 8-15: Extended Capability Pointer (EECP). Values above 0x40 are PCI-space offsets.
 * Bits 16: Hardware Prefetch Capability
 * Bits 17: Link Power Management Capability
 * Bits 18: Per-Port Change Event Capability
 * Bits 19: 32-Frame Capability
 * Bits 20-31: Reserved
 */
#define EHCI_CPARAM_64BIT                   (1 << 0)
#define EHCI_CPARAM_VARIABLEFRAMELIST       (1 << 1)
#define EHCI_CPARAM_ASYNCPARK               (1 << 2)
#define EHCI_CPARAM_ISOCTHRESHOLD(n)        ((n >> 4) & 0xF)
#define EHCI_CPARAM_EECP(n)                 ((n >> 8) & 0xFF)
#define EHCI_CPARAM_HWPREFETCH              (1 << 16)
#define EHCI_CPARAM_LINK_PWR                (1 << 17)
#define EHCI_CPARAM_PERPORT_CHANGE          (1 << 18)
#define EHCI_CPARAM_32FRAME_SUPPORT         (1 << 19)

/**
 * Command, status, schedule, and port registers for the EHCI controller.
 */
PACKED_ATYPESTRUCT(volatile, EchiOperationalRegisters, {
    reg32_t UsbCommand;
    reg32_t UsbStatus;
    reg32_t UsbIntr;
    reg32_t FrameIndex;
    reg32_t SegmentSelector;        // 4G Segment Selector
    reg32_t PeriodicListAddress;    // Periodic List
    reg32_t AsyncListAddress;        // AsyncList
    uint8_t Reserved[(0x40 - 0x1C)];
    reg32_t ConfigFlag;
    reg32_t Ports[EHCI_MAX_PORTS];
});

/**
 * EchiOperationalRegisters::UsbCommand
 */
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

/**
 * EchiOperationalRegisters::UsbStatus
 */
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

/**
 * EchiOperationalRegisters::UsbIntr
 */
#define EHCI_INTR_PROCESS               (1 << 0)
#define EHCI_INTR_PROCESSERROR          (1 << 1)
#define EHCI_INTR_PORTCHANGE            (1 << 2)
#define EHCI_INTR_FLROLLOVER            (1 << 3)
#define EHCI_INTR_HOSTERROR             (1 << 4)
#define EHCI_INTR_ASYNC_DOORBELL        (1 << 5)
#define EHCI_INTR_PERPORTCHANGE(n)      (1 << (16 + n))

/**
 * EchiOperationalRegisters::Ports[n]
 */
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

/**
 * EhciLink::LinkBits
 * Bit 0: Terminate
 * Bit 1-2: Type
 * Bit 3-4: 0
 */
#define EHCI_LINK_END     (1 << 0)
#define EHCI_LINK_iTD     0
#define EHCI_LINK_QH      (1 << 1)
#define EHCI_LINK_siTD    (2 << 1)
#define EHCI_LINK_FSTN    (3 << 1)
#define EHCI_LINK_TYPE(n) ((n >> 1) & 0x3)

/**
 * High-speed isochronous transfer descriptor, linked directly into the
 * schedule without a queue head. Must be aligned to 32 bytes.
 */
PACKED_TYPESTRUCT(EhciIsochronousDescriptor, {
    reg32_t Link;
    reg32_t Transactions[8];
    reg32_t Buffers[7];
    reg32_t ExtBuffers[7]; // 92 bytes for all this

    // Software metadata
    UsbSchedulerObject_t Object;
    uint16_t             QueueIndex;
    reg32_t              TransactionsCopy[8];
});

/**
 * EhciIsochronousDescriptor::Transaction[0-7]
 * Bit 0-11: Transaction Offset
 * Bit 12-14: Page Selector 
 * Bit 15: Interrupt on Completion 
 * Bit 16-27: Transaction Length 
 * Bit 28-31: Condition Code (Status)
 */
#define EHCI_iTD_OFFSET(n) (n & 0xFFF)
#define EHCI_iTD_PAGE(n)   ((n & 0x7) << 12)
#define EHCI_iTD_IOC       (1 << 15)
#define EHCI_iTD_LENGTH(n) ((n & 0xFFF) << 16)
#define EHCI_iTD_ACTIVE    (1 << 31)

#define EHCI_iTD_CC(n)     ((n >> 28) & 0xF)

/**
 * EhciIsochronousDescriptor::Buffers[0]
 * Bit 0-6: Device Address 
 * Bit 7: Reserved 
 * Bit 8-11: Endpoint Number 
 * Bit 12-31: Buffer Page
 */
#define EHCI_iTD_DEVADDR(n) (n & 0x7F)
#define EHCI_iTD_EPADDR(n)  ((n & 0xF) << 8)

/**
 * EhciIsochronousDescriptor::Buffers[1]
 * Bit 0-10: Maximum Packet Size (max 400h)
 * Bit 11: Direction (0 = out, 1 = in) 
 * Bit 12-31: Buffer Page
 */
#define EHCI_iTD_MPS(n) (MIN(1024, n))
#define EHCI_iTD_IN     (1 << 11)
#define EHCI_iTD_OUT    0

/**
 * EhciIsochronousDescriptor::Buffers[2]
 * Bit 0-1: How many transactions per micro-frame should be executed
 * Bit 12-31: Buffer Page
 */
#define EHCI_iTD_TRANSACTIONCOUNT(n) (n & 0x3)

/**
 * EhciIsochronousDescriptor::Buffers[3-6]
 * Bit 0-11: Reserved
 * Bit 12-31: Buffer Page
 */
#define EHCI_iTD_BUFFER(n)    (n & 0xFFFFF000)
#define EHCI_iTD_EXTBUFFER(n) ((n >> 32) & 0xFFFFFFFF)

/**
 * Full-speed isochronous transfer descriptor for a transaction translator.
 * Must be aligned to 32 bytes.
 */
PACKED_TYPESTRUCT(EhciSplitIsochronousDescriptor, {
    reg32_t  Link;
    reg32_t  Flags;
    uint8_t  FrameStartMask;
    uint8_t  FrameCompletionMask;
    uint16_t Reserved;
    reg32_t  Status;
    reg32_t  Bp0AndOffset;
    reg32_t  Bp1AndInfo;
    reg32_t  BackPointer;
    reg32_t  ExtBp0;
    reg32_t  ExtBp1;

    // Software metadata
    UsbSchedulerObject_t Object;
    reg32_t              OriginalFlags;
    reg32_t              OriginalStatus;
    reg32_t              OriginalBp0AndOffset;
    reg32_t              OriginalBp1AndInfo;
    uint8_t              CompletionFrameOffset;
    size_t               StartBandwidth;
    size_t               CompleteBandwidth;
});

/**
 * EhciSplitIsochronousDescriptor::Flags
 * Bit 0-11: Transaction Offset
 * Bit 12-14: Page Selector 
 * Bit 15: Interrupt on Completion 
 * Bit 16-27: Transaction Length 
 * Bit 28-31: Condition Code (Status)
 */
#define EHCI_siTD_XACTIONOFFSET(Offset) (Offset & 0xFFF)
#define EHCI_siTD_PAGESELECT(Page)      ((Page & 0x7) << 12)
#define EHCI_siTD_IOC                   (1 << 15)
#define EHCI_siTD_LENGTH(Length)        ((Length & 0xFFF) << 16)
#define EHCI_siTD_CC(Code)              ((Code & 0xF) << 28)

/**
 * EhciSplitIsochronousDescriptor::Status
 * Bit 0-7: Status
 * Bit 8-15: Frame Complete-split Progress Mask 
 * Bit 16-25: Transfer Length (max 3FFh)
 * Bit 26-29: Reserved
 * Bit 30: Page Select (0 = Bp0, 1 = Bp1)
 * Bit 31: IOC
 */
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

/**
 * EhciSplitIsochronousDescriptor::Bp0AndOffset
 * Bit 0-11: Current Offset 
 * Bit 12-31: Bp0
 */
#define EHCI_siTD_OFFSET(n)             (n & 0xFFF)
#define EHCI_siTD_BUFFER(n)             (n & 0xFFFFF000)

/**
 * EhciSplitIsochronousDescriptor::Bp1AndInfo
 * Bit 0-2: Transaction count, max val 6
 * Bit 3-4: Transaction Position, 
 * Bit 5-11: Reserved
 * Bit 12-31: Bp1
 */
#define EHCI_siTD_TCOUNT(n)             (MIN(6, n) & 0x7)
#define EHCI_siTD_POSITION_ALL          0
#define EHCI_siTD_POSITION_BEGIN        (1 << 3)
#define EHCI_siTD_POSITION_MID          (2 << 3)
#define EHCI_siTD_POSITION_END          (3 << 3)

/**
 * @brief Initializes a full/low-speed split isochronous TD using the frame masks selected
 * by the scheduler and the transfer's physical buffer segments.
 *
 * @param controller The EHCI controller with the address-width capabilities.
 * @param transfer The USB transfer with endpoint, hub, port, and speed information.
 * @param siTD The siTD allocated from the scheduler pool.
 * @param pid The EHCI split transaction direction, IN or OUT.
 * @param addresses Physical addresses for the transaction buffer segments.
 * @param lengths Lengths corresponding to the physical buffer segments.
 * @return true when the buffer and schedule fit in one siTD.
 */
extern bool
EHCISiTDInitialize(
    _In_ EhciController_t*                 controller,
    _In_ UsbManagerTransfer_t*              transfer,
    _In_ EhciSplitIsochronousDescriptor_t*  siTD,
    _In_ uint32_t                           pid,
    _In_ const uintptr_t*                   addresses,
    _In_ const uint32_t*                    lengths);

/**
 * @brief Checks siTD completion and errors and updates the transfer scan context.
 *
 * @param scanContext Common transfer scan state being updated.
 * @param siTD The completed or still-active siTD to inspect.
 */
extern void
EHCISiTDVerify(
    _In_ struct HCIProcessReasonScanContext* scanContext,
    _In_ EhciSplitIsochronousDescriptor_t*   siTD);

/**
 * @brief Restores the original hardware state of an siTD for a periodic retry.
 *
 * @param siTD The siTD whose software completion state is reset.
 */
extern void
EHCISiTDRestart(
    _In_ EhciSplitIsochronousDescriptor_t* siTD);

/**
 * Transfer descriptor for bulk and control transactions.
 * Hardware uses the first 52 bytes. Must be aligned to 32 bytes.
 */
PACKED_TYPESTRUCT(EhciTransferDescriptor, {
    reg32_t  Link;
    reg32_t  AlternativeLink; // Used if short-packet is detected
    uint8_t  Status;
    uint8_t  Token;
    uint16_t Length;
    reg32_t  Buffers[5];
    reg32_t  ExtBuffers[5];

    // Software metadata
    UsbSchedulerObject_t Object;
    uint8_t              OriginalToken;
    uint16_t             OriginalLength;
});

/**
 * EhciTransferDescriptor::Status
 */
#define EHCI_TD_PINGSTATE           (1 << 0)
#define EHCI_TD_SPLITXACT           (1 << 1)
#define EHCI_TD_INCOMPLETE          (1 << 2)
#define EHCI_TD_XACT                (1 << 3) // CRC/Timeout, Bad PID
#define EHCI_TD_BABBLE              (1 << 4)
#define EHCI_TD_DATABUFERROR        (1 << 5)
#define EHCI_TD_HALTED              (1 << 6)
#define EHCI_TD_ACTIVE              (1 << 7)

/**
 * EhciTransferDescriptor::Token
 * Bit 0-1: PID
 * Bit 2-3: Error Count
 * Bit 4-6: Page Selector (0-4 value)
 * Bit 7: IOC
 */
#define EHCI_TD_OUT                 0
#define EHCI_TD_IN                  (1 << 0)
#define EHCI_TD_SETUP               (1 << 1)
#define EHCI_TD_ERRCOUNT            (3 << 2)
#define EHCI_TD_PAGE(n)             (MIN(4, n) << 4)
#define EHCI_TD_IOC                 (1 << 7)

/**
 * EhciTransferDescriptor::Length
 * Bit 0-14: Length (Max Value 0x5000 (5 Pages))
 * Bit 15: Data Toggle
 */
#define EHCI_TD_LENGTHMASK          0x7FFF
#define EHCI_TD_LENGTH(n)           (MIN(20480, n))
#define EHCI_TD_TOGGLE              (1 << 15)

/**
 * EhciTransferDescriptor::Buffers[5]
 * Bit 0-11: Current Offset
 * Bit 12-31: Buffer Page
 */
#define EHCI_TD_OFFSET(n)           (n & 0xFFF)
#define EHCI_TD_BUFFER(n)           (n & 0xFFFFF000)
#define EHCI_TD_EXTBUFFER(n)        (((uint64_t)n >> 32) & 0xFFFFFFFF)

#define EHCI_TD_CERR(n)             ((n >> 10) & 0x3)
#define EHCI_TD_CC(n)               (n & 0xFF)

/**
 * Queue head work area for the current TD. Fields follow the transfer
 * descriptor layout except where described below.
 */
PACKED_TYPESTRUCT(EhciQueueHeadOverlay, {
    reg32_t  NextTD;
    reg32_t  NextAlternativeTD;
    uint8_t  Status;
    uint8_t  Token;
    uint16_t Length;
    reg32_t  Buffers[5];
    reg32_t  ExtBuffers[5];
});

/**
 * EhciQueueHeadOverlay::NextAlternativeTD
 * Bit 0: Terminate
 * Bit 1-4: NAK Count
 */
#define EHCI_TD_NAKCOUNT(Val)           ((Val >> 1) & 0xF)

/**
 * EhciQueueHeadOverlay::Buffers[1]
 * Bit 0-7: Completion Process Mask
 * Bit 8-11: Reserved
 * Bit 12-31: Buffer Page
 */
#define EHCI_TD_CPMASK(Val)             (Val & 0xFF)

/**
 * EhciQueueHeadOverlay::Buffers[2]
 * Bit 0-4: Frame Tag
 * Bit 5-11: S-Bytes
 * Bit 12-31: Buffer Page
 */
#define EHCI_TD_FRAMETAG(Val)           (Val & 0x1F)
#define EHCI_TD_SBYTES(Val)             ((Val >> 5) & 0x7F)

/**
 * Queue head with endpoint settings and the current TD work area.
 * Must be aligned to 32 bytes.
 */
PACKED_TYPESTRUCT(EhciQueueHead, {
    reg32_t                LinkPointer;
    reg32_t                Flags;
    uint8_t                FrameStartMask;
    uint8_t                FrameCompletionMask;
    uint16_t               State;
    reg32_t                Current;
    EhciQueueHeadOverlay_t Overlay; // Current working area, contains td

    // Software metadata
    UsbSchedulerObject_t Object;
});

/**
 * EhciQueueHead::Flags
 * Bit 0-6: Device Address
 * Bit 7: Inactivate on Next Transaction (Interrupt only)
 * Bit 8-11: Endpoint Number 
 * Bit 12-13: Endpoint Speed
 * Bit 14: Data Toggle Control
 * Bit 15: Head of Reclamation List Flag
 * Bit 16-26: Max Packet Length
 * Bit 27: Control Endpoint Flag
 * Bit 28-31: Nak Recount Load
 */
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

/**
 * EhciQueueHead::State
 * Bit 0-6: Hub Address
 * Bit 7-13: Port Number
 * Bit 14-15: Multi
 */
#define EHCI_QH_HUBADDR(n)              (n & 0x7F)
#define EHCI_QH_PORT(n)                 ((n & 0x7F) << 7)
#define EHCI_QH_MULTIPLIER(n)           ((n & 0x3) << 14)

/**
 * Frame span traversal node for full- and low-speed transactions that cross
 * a host-frame boundary. Use only in the periodic schedule, with HCIVERSION
 * at least 0096h (revision 0.96). Use in the asynchronous schedule or with an
 * earlier controller revision has undefined behavior.
 */
PACKED_TYPESTRUCT(EhciFSTN, {
    reg32_t PathPointer;     // HW Link (Next Periodic Element)
    reg32_t BackPathPointer; // HW Link

    // Software metadata used by the common scheduler pool.
    UsbSchedulerObject_t Object;
});

/**
 * Scheduler pool sizes and alignments for queue heads, transfer descriptors,
 * isochronous descriptors, and frame span traversal nodes.
 */
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

#define EHCI_FSTN_POOL                      4
#define EHCI_FSTN_COUNT                     50
#define EHCI_FSTN_ALIGNMENT                 32

#define EHCI_QH_NULL                        0
#define EHCI_QH_ASYNC                       1
#define EHCI_QH_START                       2

#define EHCI_TD_NULL                        0
#define EHCI_TD_START                       1

#define EHCI_iTD_NULL                       0
#define EHCI_iTD_START                      1

#define EHCI_siTD_NULL                      0
#define EHCI_siTD_START                     1

#define EHCI_FSTN_NULL                      0
#define EHCI_FSTN_START                     1

/**
 * EHCI controller registers, schedule state, and saved settings for stop retries.
 */
typedef struct EHCIController {
    UsbManagerController_t      Base;

    // Registers and resources
    EchiCapabilityRegisters_t*  CapRegisters;
    EchiOperationalRegisters_t* OpRegisters;
    size_t                      FrameCount;

    // Wait for the pending asynchronous-advance request before issuing another.
    uint32_t AdvanceIssued;
    uint32_t AdvanceCompleted;
    uint32_t AdvanceQueued;
    bool     AdvancePending;

    // On a stop retry, restore the registers saved before the first attempt.
    reg32_t ResumeCommand;
    reg32_t ResumeInterrupts;
    bool    ResumePending;

    // Cached controller capabilities
    reg32_t                     SParameters;
    reg32_t                     CParameters;
} EhciController_t;

/*******************************************************************************
 * Controller Methods
 *******************************************************************************/

/**
 * @brief Initializes the controller's queue resources and resets counters.
 */
extern
oserr_t
EhciQueueInitialize(
    _In_ EhciController_t *Controller);

/**
 * @brief Clears transfers and frees the controller's queue resources.
 * Hardware must be stopped before calling.
 *
 * @return OS_EBUSY if transfers remain, or the scheduler destruction status.
 */
extern
oserr_t
EhciQueueDestroy(
    _In_ EhciController_t *Controller);

/**
 * @brief Clears existing transfers and reinitializes the queue.
 */
extern
oserr_t
EhciQueueReset(
    _In_ EhciController_t *Controller);

/**
 * @brief Enables the schedule used by the transfer type if needed.
 */
extern
void
EhciEnableScheduler(
    _In_ EhciController_t*  Controller,
    _In_ uint8_t  Type);

/**
 * @brief Enables or disables prefetching for the transfer type.
 *
 * @param Controller The EHCI controller.
 * @param Type The USB transfer type selecting the schedule.
 * @param Set Nonzero to enable prefetching, zero to disable it.
 * @return OS_EOK on success, or OS_EUNKNOWN if prefetching is unsupported.
 */
extern
oserr_t
EhciSetPrefetching(
    _In_ EhciController_t*  Controller,
    _In_ uint8_t  Type,
    _In_ int                Set);

/**
 * @brief Halts the controller and clears pending events.
 */
extern
oserr_t
EhciHalt(
    _In_ EhciController_t *Controller);

/**
 * @brief Resets and restarts the controller and schedule after a serious failure.
 */
extern
oserr_t
EhciRestart(
    _In_ EhciController_t *Controller);

/**
 * @brief Clears the given port bits without clearing R/WC status bits.
 */
extern
void
EhciPortClearBits(
    _In_ EhciController_t*          Controller,
    _In_ int                        Index,
    _In_ reg32_t                    Bits);

/**
 * @brief Sets the given port bits without clearing R/WC status bits.
 */
extern
void
EhciPortSetBits(
    _In_ EhciController_t*          Controller,
    _In_ int                        Index,
    _In_ reg32_t                    Bits);

/**
 * @brief Scans the controller's ports and handles status changes.
 */
extern
void
EhciPortScan(
    _In_ EhciController_t*  Controller,
    _In_ reg32_t            ChangeBits);

/*******************************************************************************
 * Queue Head Methods
 *******************************************************************************/

/**
 * @brief Initializes the queue head, including periodic scheduling when needed.
 */
extern
oserr_t
EHCIQHInitialize(
        _In_ EhciController_t*     controller,
        _In_ UsbManagerTransfer_t* transfer,
        _In_ EhciQueueHead_t*      qh,
        _In_ uint8_t               deviceAddress,
        _In_ uint8_t               endpointAddress);

/**
 * @brief Dumps the queue head fields for debugging.
 */
extern
void
EHCIQHDump(
    _In_ EhciController_t*          controller,
    _In_ EhciQueueHead_t*           qh);

/**
 * @brief Restarts an interrupt queue head from its initial state.
 */
extern
void
EHCIQHRestart(
    EhciController_t*               controller,
    UsbManagerTransfer_t*           transfer);

/*******************************************************************************
 * Transfer Descriptor Methods
 *******************************************************************************/

/**
 * @brief Initializes a TD for the setup stage of a control transfer.
 * The TD must already be allocated and linked.
 *
 * @param controller The EHCI controller.
 * @param td The TD to initialize.
 * @param element The setup packet's mapped buffer segments.
 */
extern void
EHCITDSetup(
        _In_ EhciController_t*         controller,
        _In_ EhciTransferDescriptor_t* td,
        _In_ const struct TransferElement* element);

/**
 * @brief Initializes a data TD for a control, bulk, or interrupt transfer.
 * The TD must already be allocated and linked.
 *
 * @param controller The EHCI controller.
 * @param td The TD to initialize.
 * @param pid The transaction direction.
 * @param element The mapped buffer segments and transaction length.
 * @param toggle The initial data toggle.
 */
extern void
EHCITDData(
        _In_ EhciController_t*         controller,
        _In_ EhciTransferDescriptor_t* td,
        _In_ uint8_t                   pid,
        _In_ const struct TransferElement* element,
        _In_ int                       toggle);

/**
 * @brief Dumps the transfer descriptor fields for debugging.
 */
extern
void
EHCITDDump(
    _In_ EhciController_t*          controller,
    _In_ EhciTransferDescriptor_t*  td);

/**
 * @brief Checks TD completion and updates the scan context with the transferred
 * byte count and error status.
 */
extern void
EHCITDVerify(
        _In_ struct HCIProcessReasonScanContext* scanContext,
        _In_ EhciTransferDescriptor_t*           td);

/**
 * @brief Updates the pipe's data toggle from the transfer descriptor.
 */
extern
void
EhciTdSynchronize(
    _In_ UsbManagerTransfer_t*      Transfer,
    _In_ EhciTransferDescriptor_t*  Td);

/**
 * @brief Resets the TD status and advances the buffer for interrupt transfers
 * that use a circular buffer.
 */
extern
void
EHCITDRestart(
    _In_ EhciController_t*          controller,
    _In_ UsbManagerTransfer_t*      transfer,
    _In_ EhciTransferDescriptor_t*  td);

/*******************************************************************************
 * Isochronous TD Methods
 *******************************************************************************/

/**
 * @brief Initializes a high-speed isochronous TD from physical buffer segments.
 * The TD must already be allocated and linked.
 *
 * @param controller The EHCI controller.
 * @param transfer The transfer with endpoint and packet-size settings.
 * @param iTd The iTD to initialize.
 * @param pid The isochronous transaction direction.
 * @param addresses Physical addresses for up to eight transactions.
 * @param lengths Transaction lengths. A zero address or length ends the list.
 * @return true if the transactions fit in the iTD's seven buffer-page entries.
 */
extern bool
EHCITDIsochronous(
        _In_ EhciController_t*            controller,
        _In_ UsbManagerTransfer_t*        transfer,
        _In_ EhciIsochronousDescriptor_t* iTd,
        _In_ uint32_t                     pid,
        _In_ const uintptr_t*             addresses,
        _In_ const uint32_t*              lengths);

/**
 * @brief Dumps the iTD's physical address and hardware fields.
 *
 * @param controller The EHCI controller with the iTD pool.
 * @param td The iTD to dump.
 */
extern void
EHCIITDDump(
    _In_ EhciController_t*              controller,
    _In_ EhciIsochronousDescriptor_t*   td);

/**
 * @brief Dumps the physical link and hardware fields of a split isochronous TD.
 *
 * @param controller The EHCI controller with the siTD pool.
 * @param siTD The siTD to dump.
 */
extern void
EHCISiTDDump(
    _In_ EhciController_t*                 controller,
    _In_ EhciSplitIsochronousDescriptor_t* siTD);

/**
 * @brief Updates the scan context when all iTD transactions have completed.
 *
 * @param scanContext Transfer byte counts, completion counts, and error status.
 * @param iTD The iTD to inspect.
 */
extern void
EHCIITDVerify(
        _In_ struct HCIProcessReasonScanContext* scanContext,
        _In_ EhciIsochronousDescriptor_t*        iTD);

/**
 * @brief Restores the saved iTD transactions and clears software completion state.
 *
 * @param iTD The iTD to restart.
 */
extern void
EHCIITDRestart(
        _In_ EhciIsochronousDescriptor_t* iTD);

/*******************************************************************************
 * Queue Methods
 *******************************************************************************/
    
/**
 * @brief Converts an EHCI condition code to a USB transfer status.
 */
extern
enum USBTransferCode
EHCIErrorCodeToTransferStatus(
    _In_ int                    ConditionCode);

/**
 * @brief Returns the one-based index of the highest set condition bit, or zero.
 */
extern
int
EHCIConditionCodeToIndex(
    _In_ unsigned               ConditionCode);

/**
 * @brief Requests asynchronous advance if queued work exists and no request is pending.
 */
extern
void
EhciRingDoorbell(
     _In_ EhciController_t*     Controller);

/**
 * @brief Finishes preparing the transfer and queues it in the hardware schedule.
 */
extern void
__DispatchTransfer(
    _In_ EhciController_t*      Controller,
    _In_ UsbManagerTransfer_t*  Transfer);

#endif //!__USB_EHCI__

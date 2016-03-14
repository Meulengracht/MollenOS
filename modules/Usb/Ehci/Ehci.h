/* MollenOS
*
* Copyright 2011 - 2016, Philip Meulengracht
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
* MollenOS USB EHCI Controller Driver
*/

#ifndef _USB_EHCI_H_
#define _USB_EHCI_H_

/* Includes */
#include <crtdefs.h>
#include <stdint.h>

#include <Module.h>
#include <DeviceManager.h>
#include <UsbCore.h>

/* Definitions */
#define EHCI_MAX_PORTS			15
#define EHCI_STRUCT_ALIGN			32

//#define EHCI_DISABLE
#define EHCI_DIAGNOSTICS

/* Structures */

/* EHCI Register Space */

/* Capability Registers */
#pragma pack(push, 1)
typedef struct _EchiCapabilityRegisters
{
	/* Capability Registers Length 
	 * We add this offset to the Usb Base
	 * to get operational registers */
	uint8_t Length;

	/* Reserved */
	uint8_t Reserved;

	/* Interface Version Number */
	uint16_t Version;

	/* Structural Parameters 
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
	uint32_t SParams;

	/* Capability Parameters 
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
	uint32_t CParams;

	/* Companion Port Route Description */
	uint64_t PortRouting;

} EchiCapabilityRegisters_t;
#pragma pack(pop)

/* Bit Definitions */
#define EHCI_SPARAM_PORTCOUNT(n)			(n & 0xF)
#define EHCI_SPARAM_PPC						(1 << 4)
#define EHCI_SPARAM_PORTROUTING				(1 << 7)

/* Ports per companion controller */
#define EHCI_SPARAM_CCPCOUNT(n)				((n >> 8) & 0xF)

/* Number of companion controllers */
#define EHCI_SPARAM_CCCOUNT(n)				((n >> 12) & 0xF)
#define EHCI_SPARAM_PORTINDICATORS			(1 << 16)
#define EHCI_SPARAM_DEBUGPORT(n)			((n >> 20) & 0xF)

#define EHCI_CPARAM_64BIT					(1 << 0)
#define EHCI_CPARAM_VARIABLEFRAMELIST		(1 << 1)
#define EHCI_CPARAM_ASYNCPARK				(1 << 2)
#define EHCI_CPARAM_ISOCTHRESHOLD(n)		((n >> 4) & 0xF)
#define EHCI_CPARAM_EECP(n)					((n >> 8) & 0xFF)
#define EHCI_CPARAM_HWPREFECT				(1 << 16)
#define EHCI_CPARAM_LINK_PWR				(1 << 17)
#define EHCI_CPARAM_PERPORT_CHANGE			(1 << 18)

/* Operational Registers */
#pragma pack(push, 1)
typedef struct _EchiOperationalRegisters
{
	/* USB Command Register */
	uint32_t UsbCommand;

	/* USB Status Register */
	uint32_t UsbStatus;

	/* USB Interrupt Register */
	uint32_t UsbIntr;

	/* Frame Index */
	uint32_t FrameIndex;

	/* 4G Segment Selector */
	uint32_t SegmentSelector;

	/* Periodic Frame List Base Address */
	uint32_t PeriodicListAddr;

	/* Asynchronous List Address 
	 * Get's executed after Periodic List */
	uint32_t AsyncListAddress;

	/* Reserved */
	uint8_t Reserved[(0x40 - 0x1C)];

	/* Configured Flag Register */
	uint32_t ConfigFlag;

	/* Port Status Registers */
	uint32_t Ports[EHCI_MAX_PORTS];

} EchiOperationalRegisters_t;
#pragma pack(pop)

/* Command Bits */
#define EHCI_COMMAND_RUN				(1 << 0)
#define EHCI_COMMAND_HCRESET			(1 << 1)
#define EHCI_COMMAND_LISTSIZE(n)		((n & 0x3) << 2)
#define EHCI_COMMAND_PERIODIC_ENABLE	(1 << 4)
#define EHCI_COMMAND_ASYNC_ENABLE		(1 << 5)
#define EHCI_COMMAND_IOC_ASYNC_DOORBELL	(1 << 6)
#define EHCI_COMMAND_LIGHT_HCRESET		(1 << 7)
#define EHCI_COMMAND_PARK_COUNT(n)		((n & 0x3) << 8)
#define EHCI_COMMAND_ASYNC_PARKMODE		(1 << 11)
#define EHCI_COMMAND_PERIOD_PREFECTCH	(1 << 12)
#define EHCI_COMMAND_ASYNC_PREFETCH		(1 << 13)
#define EHCI_COMMAND_FULL_PREFETCH		(1 << 14)
#define EHCI_COMMAND_PERPORT_ENABLE		(1 << 15)
#define EHCI_COMMAND_INTR_THRESHOLD(n)	((n & 0xFF) << 16)
#define EHCI_COMMAND_HI_RESUME(n)		((n & 0xF) << 23)

#define EHCI_LISTSIZE_1024				0
#define EHCI_LISTSIZE_512				1
#define EHCI_LISTSIZE_256				2
#define EHCI_LISTSIZE_32				3

/* Status Bits */
#define EHCI_STATUS_PROCESS				(1 << 0)
#define EHCI_STATUS_PROCESSERROR		(1 << 1)
#define EHCI_STATUS_PORTCHANGE			(1 << 2)
#define EHCI_STATUS_FLROLLOVER			(1 << 3)
#define EHCI_STATUS_HOSTERROR			(1 << 4)
#define EHCI_STATUS_ASYNC_DOORBELL		(1 << 5)

#define EHCI_STATUS_HALTED				(1 << 12)
#define EHCI_STATUS_RECLAMATION			(1 << 13)
#define EHCI_STATUS_PERIODIC_ACTIVE		(1 << 14)
#define EHCI_STATUS_ASYNC_ACTIVE		(1 << 15)
#define EHCI_STATUS_PERPORTCHANGE(n)	(1 << (16 + n))

/* INTR Bits */
#define EHCI_INTR_PROCESS				(1 << 0)
#define EHCI_INTR_PROCESSERROR			(1 << 1)
#define EHCI_INTR_PORTCHANGE			(1 << 2)
#define EHCI_INTR_FLROLLOVER			(1 << 3)
#define EHCI_INTR_HOSTERROR				(1 << 4)
#define EHCI_INTR_ASYNC_DOORBELL		(1 << 5)
#define EHCI_INTR_PERPORTCHANGE(n)		(1 << (16 + n))

/* Port Command Bits */
#define EHCI_PORT_CONNECTED				(1 << 0)
#define EHCI_PORT_CONNECT_EVENT			(1 << 1)
#define EHCI_PORT_ENABLED				(1 << 2)
#define EHCI_PORT_ENABLE_EVENT			(1 << 3)

#define EHCI_PORT_OC_EVENT				(1 << 5)
#define EHCI_PORT_FORCERESUME			(1 << 6)
#define EHCI_PORT_SUSPENDED				(1 << 7)
#define EHCI_PORT_RESET					(1 << 8)

#define EHCI_PORT_LINESTATUS(n)			((n >> 10) & 0x3)
#define EHCI_LINESTATUS_RELEASE			0x1

#define EHCI_PORT_POWER					(1 << 12)
#define EHCI_PORT_COMPANION_HC			(1 << 13)

/* Port Wake Bits */
#define EHCI_PORT_CONNECT_WAKE			(1 << 20)
#define EHCI_PORT_DISCONNECT_WAKE		(1 << 21)
#define EHCI_PORT_OC_WAKE				(1 << 22)

/* Link Bits */
#define EHCI_LINK_END					(1 << 0)
#define EHCI_LINK_iTD					0
#define EHCI_LINK_QH					(1 << 1)
#define EHCI_LINK_siTD					(2 << 1)
#define EHCI_LINK_FSTN					(3 << 1)
#define EHCI_LINK_TYPE(n)				((n >> 1) & 0x3)

/* Isochronous Transfer Descriptor 
 * Must be 32 byte aligned */
#pragma pack(push, 1)
typedef struct _EhciIsocDescriptor
{
	/* Link Pointer 
	 * Bit 0: Terminate
	 * Bit 1-2: Type
	 * Bit 3-4: 0 */
	uint32_t Link;

	/* Transactions 
	 * Bit 0-11: Transaction Offset
	 * Bit 12-14: Page Selector 
	 * Bit 15: Interrupt on Completion 
	 * Bit 16-27: Transaction Length 
	 * Bit 28-31: Condition Code (Status) */
	uint16_t Transaction0[2];
	uint16_t Transaction1[2];
	uint16_t Transaction2[2];
	uint16_t Transaction3[2];
	uint16_t Transaction4[2];
	uint16_t Transaction5[2];
	uint16_t Transaction6[2];
	uint16_t Transaction7[2];

	/* Status & Bp0 
	 * Bit 0-6: Device Address 
	 * Bit 7: Reserved 
	 * Bit 8-11: Endpoint Number 
	 * Bit 12-31: Buffer Page */
	uint32_t StatusAndBp0;

	/* MaxPacketSize & Bp1 
	 * Bit 0-10: Maximum Packet Size (max 400h)
	 * Bit 11: Direction (0 = out, 1 = in) 
	 * Bit 12-31: Buffer Page */
	uint32_t MpsAndBp1;

	/* Multi & Bp2
	 * Bit 0-1: How many transactions per micro-frame should be executed
	 * Bit 12-31: Buffer Page */
	uint32_t MultAndBp2;

	/* Bp3-6 
	 * Bit 0-11: Reserved
	 * Bit 12-31: Buffer Page */
	uint32_t Bp3;
	uint32_t Bp4;
	uint32_t Bp5;
	uint32_t Bp6;

	/* Extended buffer pages
	* Their upper address bits */
	uint32_t ExtBp0;
	uint32_t ExtBp1;
	uint32_t ExtBp2;
	uint32_t ExtBp3;
	uint32_t ExtBp4;
	uint32_t ExtBp5;
	uint32_t ExtBp6;

} EhciIsocDescriptor_t;
#pragma pack(pop)

/* iTD: Transaction Bits */
#define EHCI_iTD_DEVADDR(n)				(n & 0x7F)
#define EHCI_iTD_EPADDR(n)				((n & 0xF) << 8)
#define EHCI_iTD_OFFSET(n)				(n & 0xFFF)
#define EHCI_iTD_PAGE(n)				((n & 0x7) << 12)
#define EHCI_iTD_IOC					(1 << 15)
#define EHCI_iTD_LENGTH(n)				(n & 0xFFF)
#define EHCI_iTD_ACTIVE					(1 << 15)
#define EHCI_iTD_CC(n)					((n >> 12) & 0xF)

#define EHCI_iTD_MPS(n)					(MIN(1024, n))
#define EHCI_iTD_IN						(1 << 11);
#define EHCI_iTD_OUT					0
#define EHCI_iTD_BUFFER(n)				(n & 0xFFFFF000)
#define EHCI_iTD_EXTBUFFER(n)			((n >> 32) & 0xFFFFFFFF)
#define EHCI_iTD_TRANSACTIONCOUNT(n)	(n & 0x3)

/* Split Isochronous Transfer Descriptor
* Must be 32 byte aligned */
#pragma pack(push, 1)
typedef struct _EhciSplitIsocDescriptor
{
	/* Link Pointer
	* Bit 0: Terminate
	* Bit 1-2: Type
	* Bit 3-4: 0 */
	uint32_t Link;

	/* Flags 
	 * Bit 0-6: Device Address
	 * Bit 7: Reserved
	 * Bit 8-11: Endpoint Address 
	 * Bit 12-15: Reserved
	 * Bit 16-22: Hub Address
	 * Bit 23: Reserved
	 * Bit 24-30: Port Number
	 * Bit 31: Direction */
	uint32_t Flags;

	/* Frame Masks */
	uint8_t FStartMask;
	uint8_t FCompletionMask;

	/* Reserved */
	uint16_t Reserved;

	/* Status
	 * Bit 0-7: Status
	 * Bit 8-15: µFrame Complete-split Progress Mask 
	 * Bit 16-25: Transfer Length (max 3FFh)
	 * Bit 26-29: Reserved
	 * Bit 30: Page Select (0 = Bp0, 1 = Bp1)
	 * Bit 31: IOC */
	uint32_t Status;

	/* Buffer Page 0 + Offset 
	 * Bit 0-11: Current Offset 
	 * Bit 12-31: Bp0 */
	uint32_t Bp0AndOffset;

	/* Buffer Page 1 + Info 
	 * Bit 0-2: Transaction count, max val 6
	 * Bit 3-4: Transaction Position, 
	 * Bit 5-11: Reserved
	 * Bit 12-31: Bp1 */
	uint32_t Bp1AndInfo;

	/* Back Pointer
	 * Bit 0: Terminate 
	 * Bit 1-4: Reserved */
	uint32_t BackPointer;

	/* Extended buffer pages
	* Their upper address bits */
	uint32_t ExtBp0;
	uint32_t ExtBp1;

} EhciSplitIsocDescriptor_t;
#pragma pack(pop)

/* siTD Bits */
#define EHCI_siTD_DEVADDR(n)			(n & 0x7F)
#define EHCI_siTD_EPADDR(n)				((n & 0xF) << 8)
#define EHCI_siTD_HUBADDR(n)			((n & 0x7F) << 16)
#define EHCI_siTD_PORT(n)				((n & 0x7F) << 24)
#define EHCI_siTD_OUT					0
#define EHCI_siTD_IN					(1 << 31)

#define EHCI_siTD_SMASK(n)				(n & 0xFF)
#define EHCI_siTD_CMASK(n)				((n & 0xFF) << 8)

#define EHCI_siTD_ACTIVE				(1 << 7)
#define EHCI_siTD_CC(n)					(n & 0xFF)
#define EHCI_siTD_CSPMASK(n)			((n & 0xFF) << 8)
#define EHCI_siTD_LENGTH(n)				((n & 0x3FF) << 16)
#define EHCI_siTD_PAGE(n)				((n & 0x1) << 30)
#define EHCI_siTD_IOC					(n << 31)

#define EHCI_siTD_OFFSET(n)				(n & 0xFFF)
#define EHCI_siTD_BUFFER(n)				(n & 0xFFFFF000)

#define EHCI_siTD_TCOUNT(n)				(MIN(6, n) & 0x7)
#define EHCI_siTD_POSITION_ALL			0
#define EHCI_siTD_POSITION_BEGIN		(1 << 3)
#define EHCI_siTD_POSITION_MID			(2 << 3)
#define EHCI_siTD_POSITION_END			(3 << 3)

/* Transfer Descriptor
* Must be 32 byte aligned */
#pragma pack(push, 1)
typedef struct _EhciTransferDescriptor
{
	/* Link Pointer - Next TD
	* Bit 0: Terminate */
	uint32_t Link;

	/* Alternative Link Pointer - Next TD
	* Only used when a short packet is detected
	* Then this link is executed instead (if set..)
	* Bit 0: Terminate */
	uint32_t AlternativeLink;

	/* Status */
	uint8_t Status;

	/* Token
	* Bit 0-1: PID
	* Bit 2-3: Error Count
	* Bit 4-6: Page Selector (0-4 value)
	* Bit 7: IOC */
	uint8_t Token;

	/* Transfer Length
	 * Bit 0-14: Length (Max Value 0x5000 (5 Pages))
	 * Bit 15: Data Toggle */
	uint16_t Length;

	/* Buffer Page 0 + Offset 
	 * Bit 0-11: Current Offset
	 * Bit 12-31: Buffer Page */
	uint32_t Buffers[5];

	/* Extended buffer pages
	* Their upper address bits */
	uint32_t ExtBuffers[5];

	/* 52 bytes, lets reach 64 */

	/* HCD Flags */
	uint32_t HcdFlags;
	uint32_t PhysicalAddress;

	/* Padding */
	uint32_t Padding[1];

} EhciTransferDescriptor_t;
#pragma pack(pop)

/* TD Bits */
#define EHCI_TD_PINGSTATE			(1 << 0)
#define EHCI_TD_SPLITXACT			(1 << 1)

#define EHCI_TD_INCOMPLETE			(1 << 2)

/* CRC/Timeout, Bad PID */
#define EHCI_TD_XACT				(1 << 3)

#define EHCI_TD_BABBLE				(1 << 4)
#define EHCI_TD_DATABUFERROR		(1 << 5)
#define EHCI_TD_HALTED				(1 << 6)
#define EHCI_TD_ACTIVE				(1 << 7)

#define EHCI_TD_OUT					0
#define EHCI_TD_IN					(1 << 0)
#define EHCI_TD_SETUP				(1 << 1)
#define EHCI_TD_ERRCOUNT			(3 << 2)
#define EHCI_TD_PAGE(n)				(MIN(4, n) << 4)
#define EHCI_TD_IOC					(1 << 7)

#define EHCI_TD_LENGTH(n)			(MIN(20480, n))
#define EHCI_TD_TOGGLE				(1 << 15)

#define EHCI_TD_OFFSET(n)			(n & 0xFFF)
#define EHCI_TD_BUFFER(n)			(n & 0xFFFFF000)
#define EHCI_TD_EXTBUFFER(n)		((n >> 32) & 0xFFFFFFFF)

#define EHCI_TD_CERR(n)				((n >> 10) & 0x3)
#define EHCI_TD_CC(n)				(n & 0xFF)

#define EHCI_TD_ALLOCATED			(1 << 0)
#define EHCI_TD_IBUF(n)				((n & 0xFF) << 8)
#define EHCI_TD_JBUF(n)				((n & 0xF) << 16)

#define EHCI_TD_GETIBUF(n)			((n >> 8) & 0xFF)
#define EHCI_TD_GETJBUF(n)			((n >> 16) & 0xF)

/* Queue Head Overlay */
#pragma pack(push, 1)
typedef struct _EhciQueueHeadOverlay
{
	/* Next TD Pointer
	* Bit 0: Terminate
	* Bit 1-4: Reserved */
	uint32_t NextTD;

	/* Alternative TD Pointer
	* Bit 0: Terminate
	* Bit 1-4: NAK Count */
	uint32_t NextAlternativeTD;

	/* Status */
	uint8_t Status;

	/* Token
	* Bit 0-1: PID
	* Bit 2-3: Error Count
	* Bit 4-6: Page Selector (0-4 value)
	* Bit 7: IOC */
	uint8_t Token;

	/* Transfer Length
	* Bit 0-14: Length (Max Value 0x5000 (5 Pages))
	* Bit 15: Data Toggle */
	uint16_t Length;

	/* Buffer Page 0 + Offset
	* Bit 0-11: Current Offset
	* Bit 12-31: Buffer Page */
	uint32_t Bp0AndOffset;

	/* Buffer Page 1 + Completion Process Mask
	* Bit 0-7: Completion Process Mask
	* Bit 8-11: Reserved
	* Bit 12-31: Buffer Page */
	uint32_t Bp1;

	/* Buffer Page 2 + FrameTag
	* Bit 0-4: Frame Tag
	* Bit 5-11: S-Bytes
	* Bit 12-31: Buffer Page */
	uint32_t Bp2;

	/* Buffer Page 3-4
	* Bit 0-11: Reserved
	* Bit 12-31: Buffer Page */
	uint32_t Bp3;
	uint32_t Bp4;

	/* Extended buffer pages
	* Their upper address bits */
	uint32_t ExtBp0;
	uint32_t ExtBp1;
	uint32_t ExtBp2;
	uint32_t ExtBp3;
	uint32_t ExtBp4;

} EhciQueueHeadOverlay_t;
#pragma pack(pop)

/* Queue Head
* Must be 32 byte aligned */
#pragma pack(push, 1)
typedef struct _EhciQueueHead
{
	/* Queue Head Link Pointer 
	 * Bit 0: Terminate
	 * Bit 1-2: Type
	 * Bit 3-4: Reserved */
	uint32_t LinkPointer;

	/* Flags 
	 * Bit 0-6: Device Address
	 * Bit 7: Inactivate on Next Transaction
	 * Bit 8-11: Endpoint Number 
	 * Bit 12-13: Endpoint Speed
	 * Bit 14: Data Toggle Control
	 * Bit 15: Head of Reclamation List Flag
	 * Bit 16-26: Max Packet Length
	 * Bit 27: Control Endpoint Flag
	 * Bit 28-31: Nak Recount Load */
	uint32_t Flags;

	/* Frame Masks */
	uint8_t FStartMask;
	uint8_t FCompletionMask;

	/* State 
	 * Bit 0-6: Hub Address
	 * Bit 7-13: Port Number
	 * Bit 14-15: Multi */
	uint16_t State;

	/* Current TD Pointer 
	 * Bit 0-4: Reserved */
	uint32_t CurrentTD;

	/* Overlay, represents 
	 * the current transaction state */
	EhciQueueHeadOverlay_t Overlay;

	/* 72 bytes 
	 * Add 24 bytes for allocation easieness */
	uint32_t HcdFlags;
	uint32_t PhysicalAddress;

	/* Virtual Link */
	uint32_t LinkPointerVirtual;

	/* Bandwidth */
	uint16_t sFrame;
	uint16_t Reserved;

	uint16_t Interval;
	uint16_t Period;

	uint16_t Bandwidth;
	uint8_t sMicroPeriod;
	uint8_t sPeriod;

} EhciQueueHead_t;
#pragma pack(pop)

/* QH Bits */
#define EHCI_QH_DEVADDR(n)			(n & 0x7F)
#define EHCI_QH_INVALIDATE_NEXT		(1 << 7)
#define EHCI_QH_EPADDR(n)			((n & 0xF) << 8)
#define EHCI_QH_LOWSPEED			(1 << 12)
#define EHCI_QH_FULLSPEED			0
#define EHCI_QH_HIGHSPEED			(1 << 13)
#define EHCI_QH_DTC					(1 << 14)
#define EHCI_QH_RECLAMATIONHEAD		(1 << 15)
#define EHCI_QH_MAXLENGTH(n)		(MIN(1024, n) << 16)
#define EHCI_QH_CONTROLEP			(1 << 27)
#define EHCI_QH_RL(n)				((n & 0xF) << 28)

#define EHCI_QH_HUBADDR(n)			(n & 0x7F)
#define EHCI_QH_PORT(n)				((n & 0x7F) << 7)
#define EHCI_QH_MULTIPLIER(n)		((n & 0x3) << 14)

#define EHCI_QH_OFFSET(n)			(n & 0xFFF)
#define EHCI_QH_BUFFER(n)			(n & 0xFFFFF000)
#define EHCI_QH_EXTBUFFER(n)		((n >> 32) & 0xFFFFFFFF)

#define EHCI_QH_ALLOCATED			(1 << 0)
#define EHCI_QH_UNSCHEDULE			(1 << 1)

/* Periodic Frame Span Traversal Node
* Must be 32 byte aligned */
#pragma pack(push, 1)
typedef struct _EhciFSTN
{
	/* Normal Path Pointer 
	* Bit 0: Terminate
	* Bit 1-2: Type
	* Bit 3-4: Reserved */
	uint32_t PathPointer;

	/* Back Path Link Pointer
	* Bit 0: Terminate
	* Bit 1-2: Type
	* Bit 3-4: Reserved */
	uint32_t BackPathPointer;

} EhciFSTN_t;
#pragma pack(pop)

/* Generic Link Format 
 * for iterating the 
 * periodic list */
typedef union {

	/* Different Links */
	EhciQueueHead_t *Qh;
	EhciTransferDescriptor_t *Td;
	EhciIsocDescriptor_t *iTd;
	EhciSplitIsocDescriptor_t *siTd;
	EhciFSTN_t *FSTN;
	Addr_t Address;

} EhciGenericLink_t;

/* Endpoint Data */
typedef struct _EhciEndpoint
{
	/* Td's allocated */
	size_t TdsAllocated;
	size_t BuffersAllocated;

	/* TD Pool */
	EhciTransferDescriptor_t **TDPool;

	/* Buffer Pool */
	Addr_t **BufferPool;
	int *BufferPoolStatus;

	/* Lock */
	Spinlock_t Lock;

} EhciEndpoint_t;

/* Pool Definitions */
#define EHCI_POOL_NUM_QH				60
#define EHCI_POOL_TD_SIZE				15
#define EHCI_POOL_BUFFER_MIN			5
#define EHCI_POOL_BUFFER_ALLOCATED		1
#define EHCI_BANDWIDTH_PHASES			64

/* Pool Indices */
#define EHCI_POOL_QH_NULL				0
#define EHCI_POOL_QH_ASYNC				1

/* The Controller */
typedef struct _EhciController
{
	/* Id */
	int Id;
	int HcdId;

	/* Device */
	MCoreDevice_t *Device;

	/* Lock */
	Spinlock_t Lock;
	
	/* Registers */
	volatile EchiCapabilityRegisters_t *CapRegisters;
	volatile EchiOperationalRegisters_t *OpRegisters;

	/* Copy of SP/CP */
	uint32_t SParameters;
	uint32_t CParameters;

	/* FrameList */
	size_t FLength;
	uint32_t *FrameList;
	uint32_t *VirtualList;

	/* Pools */
	EhciQueueHead_t *QhPool[EHCI_POOL_NUM_QH];
	EhciTransferDescriptor_t *TdAsync;

	/* Bandwidth */
	int Bandwidth[EHCI_BANDWIDTH_PHASES];

	/* Transactions */
	int AsyncTransactions;
	int BellIsRinging;
	int BellReScan;

	/* Port Count */
	size_t Ports;

	/* Transaction List
	* Contains transactions
	* in progress */
	void *TransactionList;

} EhciController_t;

/* Prototypes */
_CRT_EXTERN void EhciInitQueues(EhciController_t *Controller);

/* Endpoint Prototypes */
_CRT_EXTERN void EhciEndpointSetup(void *cData, UsbHcEndpoint_t *Endpoint);
_CRT_EXTERN void EhciEndpointDestroy(void *cData, UsbHcEndpoint_t *Endpoint);

/* Transaction Prototypes */
_CRT_EXTERN void EhciTransactionInit(void *cData, UsbHcRequest_t *Request);
_CRT_EXTERN UsbHcTransaction_t *EhciTransactionSetup(void *cData, UsbHcRequest_t *Request);
_CRT_EXTERN UsbHcTransaction_t *EhciTransactionIn(void *cData, UsbHcRequest_t *Request);
_CRT_EXTERN UsbHcTransaction_t *EhciTransactionOut(void *cData, UsbHcRequest_t *Request);
_CRT_EXTERN void EhciTransactionSend(void *cData, UsbHcRequest_t *Request);
_CRT_EXTERN void EhciTransactionDestroy(void *cData, UsbHcRequest_t *Request);

/* Processing Functions */
_CRT_EXTERN void EhciProcessTransfers(EhciController_t *Controller);
_CRT_EXTERN void EhciProcessDoorBell(EhciController_t *Controller);

#endif
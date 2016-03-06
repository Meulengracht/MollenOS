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

/* Definitions */
#define EHCI_MAX_PORTS			15

#define EHCI_STRUCT_ALIGN			32
#define EHCI_STRUCT_ALIGN_BITS		0x1F

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
	 * Bits 16-31: Reserved */
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
#define EHCI_COMMAND_PERIODIC_ENABLE	(1 << 4)
#define EHCI_COMMAND_ASYNC_ENABLE		(1 << 5)
#define EHCI_COMMAND_IOC_ASYNC_DOORBELL	(1 << 6)
#define EHCI_COMMAND_LIGHT_HCRESET		(1 << 7)

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

/* INTR Bits */
#define EHCI_INTR_PROCESS				(1 << 0)
#define EHCI_INTR_PROCESSERROR			(1 << 1)
#define EHCI_INTR_PORTCHANGE			(1 << 2)
#define EHCI_INTR_FLROLLOVER			(1 << 3)
#define EHCI_INTR_HOSTERROR				(1 << 4)
#define EHCI_INTR_ASYNC_DOORBELL		(1 << 5)

/* Port Command Bits */
#define EHCI_PORT_CONNECTED				(1 << 0)
#define EHCI_PORT_CONNECT_EVENT			(1 << 1)
#define EHCI_PORT_ENABLED				(1 << 2)
#define EHCI_PORT_ENABLE_EVENT			(1 << 3)

#define EHCI_PORT_FORCERESUME			(1 << 6)
#define EHCI_PORT_SUSPENDED				(1 << 7)
#define EHCI_PORT_RESET					(1 << 8)

#define EHCI_PORT_LINESTATUS(n)			((n >> 10) & 0x3)
#define EHCI_LINESTATUS_RELEASE			0x1

#define EHCI_PORT_POWER					(1 << 12)
#define EHCI_PORT_OWNER					(1 << 13)

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

/* Isochronous Transfer Descriptor 
 * Must be 32 byte aligned 
 * 32 Bit Version */
#pragma pack(push, 1)
typedef struct _EhciIsocDescriptor32
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

} EhciIsocDescriptor32_t;
#pragma pack(pop)

/* Isochronous Transfer Descriptor
* Must be 32 byte aligned
* 64 Bit Version */
#pragma pack(push, 1)
typedef struct _EhciIsocDescriptor64
{
	/* Reuse the 32 bit descriptor
	 * Same information */
	EhciIsocDescriptor32_t Header;

	/* Extended buffer pages 
	 * Their upper address bits */
	uint32_t ExtBp0;
	uint32_t ExtBp1;
	uint32_t ExtBp2;
	uint32_t ExtBp3;
	uint32_t ExtBp4;
	uint32_t ExtBp5;
	uint32_t ExtBp6;

} EhciIsocDescriptor64_t;
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

#define EHCI_iTD_MPS(n)					(n & 0x7FF)
#define EHCI_iTD_IN						(1 << 11);
#define EHCI_iTD_OUT					0
#define EHCI_iTD_BUFFER(n)				(n & 0xFFFFF000)
#define EHCI_iTD_EXTBUFFER(n)			((n >> 32) & 0xFFFFFFFF)
#define EHCI_iTD_TRANSACTIONCOUNT(n)	(n & 0x3)

/* Split Isochronous Transfer Descriptor
* Must be 32 byte aligned
* 32 Bit Version */
#pragma pack(push, 1)
typedef struct _EhciSplitIsocDescriptor32
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

} EhciSplitIsocDescriptor32_t;
#pragma pack(pop)

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

	/* Port Count */
	size_t Ports;

	/* Transaction List
	* Contains transactions
	* in progress */
	void *TransactionList;

} EhciController_t;

#endif
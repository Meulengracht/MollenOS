/* MollenOS
*
* Copyright 2011 - 2014, Philip Meulengracht
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
* MollenOS X86-32 USB UHCI Controller Driver
*/

#ifndef _X86_USB_UHCI_H_
#define _X86_USB_UHCI_H_

/* Includes */
#include <crtdefs.h>
#include <stdint.h>
#include <Pci.h>
#include <Arch.h>

/* Definitions */
#define X86_UHCI_MAX_PORTS				7
#define X86_UHCI_STRUCT_ALIGN			16
#define X86_UHCI_STRUCT_ALIGN_BITS		0xF
#define X86_UHCI_NUM_FRAMES				1024
#define X86_UHCI_USBLEG					0xC0
#define X86_UHCI_USBRES_INTEL			0xC4

/* Registers */
#define X86_UHCI_REGISTER_COMMAND		0x00
#define X86_UHCI_REGISTER_STATUS		0x02
#define X86_UHCI_REGISTER_INTR			0x04
#define X86_UHCI_REGISTER_FRNUM			0x06
#define X86_UHCI_REGISTER_FRBASEADDR	0x08
#define X86_UHCI_REGISTER_SOFMOD		0x0C
#define X86_UHCI_REGISTER_PORT_BASE		0x10


/* Command bit switches */
#define X86_UHCI_CMD_RUN				0x1
#define X86_UHCI_CMD_HCRESET			0x2
#define X86_UHCI_CMD_GRESET				0x4
#define X86_UHCI_CMD_EGSM				0x8
#define X86_UHCI_CMD_FGR				0x10
#define X86_UHCI_CMD_SWDBG				0x20
#define X86_UHCI_CMD_CF					0x40
#define X86_UHCI_CMD_MAXPACKET64		0x80

/* Status bit switches */
#define X86_UHCI_STATUS_USBINT			0x1
#define X86_UHCI_STATUS_INTR_ERROR		0x2
#define X86_UHCI_STATUS_RESUME_DETECT	0x4
#define X86_UHCI_STATUS_HOST_SYSERR		0x8
#define X86_UHCI_STATUS_PROCESS_ERR		0x10
#define X86_UHCI_STATUS_HALTED			0x20

/* Interrupt bit switches */
#define X86_UHCI_INTR_TIMEOUT			0x1
#define X86_UHCI_INTR_RESUME			0x2
#define X86_UHCI_INTR_COMPLETION		0x4
#define X86_UHCI_INTR_SHORT_PACKET		0x8

/* Port bit switches */
#define X86_UHCI_PORT_CONNECT_STATUS	0x1
#define X86_UHCI_PORT_CONNECT_EVENT		0x2
#define X86_UHCI_PORT_ENABLED			0x4
#define X86_UHCI_PORT_ENABLED_EVENT		0x8
#define X86_UHCI_PORT_LINE_STATUS		0x30
#define X86_UHCI_PORT_RESUME_DETECT		0x40
#define X86_UHCI_PORT_RESERVED			0x80
#define X86_UHCI_PORT_LOWSPEED			0x100
#define X86_UHCI_PORT_RESET				0x200
#define X86_UHCI_PORT_RESERVED1			0x400
#define X86_UHCI_PORT_RESERVED2			0x800
#define X86_UHCI_PORT_SUSPEND			0x1000

/* Structures */
#define X86_UHCI_STRUCT_ALIGN		32
#define X86_UHCI_STRUCT_ALIGN_BITS	0x1F

/* Must be 16 byte aligned */
typedef struct _UhciTransferDescriptor
{
	/* Link Pointer 
	 * Bit 0: If set, this field is invalid 
	 * Bit 1: 1 = QH, 0 = TD.
	 * Bit 2: 1 = Depth, 0 = Breadth.
	 * Bit 3: Must be 0 */
	uint32_t LinkPtr;

	/* Control & Status 
	 * Bit 0-10: Actual Length (Bytes Transfered)
	 * Bit 11-16: Reserved
	 * Bit 17: Bitstuff Error 
	 * Bit 18: CRC/Timeout Error
	 * Bit 19: NAK Recieved
	 * Bit 20: Babble Detected 
	 * Bit 21: Data Buffer Error
	 * Bit 22: Stalled
	 * Bit 23: Active 
	 * Bit 24: Interrupt on Completion
	 * Bit 25: If set, this is isochronous TD 
	 * Bit 26: Lowspeed Transfer
	 * Bit 27-28: Error Count
	 * Bit 29: Short Packet Detection
	 * Bit 30-31: Reserved */
	uint32_t Flags;

	/* Packet Header
	* Bit 0-7: PID. IN (0x69), OUT (E1), SETUP (2D) 
	* Bit 8-14: Device Address
	* Bit 15-18: Endpoint Address
	* Bit 19: Data Toggle
	* Bit 20: Reserved
	* Bit 21-31: Maximum Length */
	uint32_t Header;

	/* Buffer Pointer */
	uint32_t Buffer;

	/* 4 Dwords for software use */
	uint32_t Reserved[4];

} UhciTransferDescriptor_t;

/* Link bit switches */
#define X86_UHCI_TD_LINK_INVALID		0x1
#define X86_UHCI_TD_LINK_QH				0x2
#define X86_UHCI_TD_LINK_DEPTH			0x4

/* Control / Status bit switches */
#define X86_UHCI_TD_ACTUAL_LENGTH_BITS	0x7FF
#define X86_UHCI_TD_CTRL_ACTIVE			0x800000
#define X86_UHCI_TD_IOC					0x1000000
#define X86_UHCI_TD_ISOCHRONOUS			0x2000000
#define X86_UHCI_TD_LOWSPEED			0x4000000
#define X86_UHCI_TD_SET_ERR_CNT(n)		((n & 0x3) << 27)
#define X86_UHCI_TD_ERROR_COUNT(n)		((n & 0x18000000) >> 27)
#define X86_UHCI_TD_STATUS(n)			((n & 0x7E0000) >> 17)

/* Header bit switches */
#define X86_UHCI_TD_PID_SETUP			0x2D
#define X86_UHCI_TD_PID_IN				0x69
#define X86_UHCI_TD_PID_OUT				0xE1
#define X86_UHCI_TD_DEVICE_ADDR(n)		((n & 0x7F) << 8)
#define X86_UHCI_TD_EP_ADDR(n)			((n & 0xF) << 15)
#define X86_UHCI_TD_DATA_TOGGLE(n)		(n << 19)
#define X86_UHCI_TD_MAX_LEN(n)			((n & 0x7FF) << 21)

/* Queue Head, 16 byte align */
typedef struct _UhciQueueHead
{
	/* Queue Head Link Pointer 
	* Bit 0 - Terminate if set
	* Bit 1 - 1 = QH, 0 = TD */
	uint32_t HeadPtr;

	/* Queue Element Link Pointer
	 * Bit 0 - Terminate if set
	 * Bit 1 - 1 = QH, 0 = TD */
	uint32_t ElementPtr;

	/* Controller Driver Specific 
	 * Bit 0: If set, this is in use 
	 * Bit 1-7: Pool Number
	 * Bit 8-15: Queue Head Index 
	 * Bit 16-17: Queue Head Type (00 Control, 01 Bulk, 10 Interrupt, 11 Isochronous) */
	uint32_t Flags;

	/* Driver Data */
	uint32_t HcdData;

	/* Virtual Address of next QH */
	uint32_t NextHeadVirt;

	/* Virtual Address of TD Head */
	uint32_t TdHeadVirt;

	/* Padding */
	uint32_t Padding[2];

} UhciQueueHead_t;

/* Flag bit switches */
#define X86_UHCI_QH_ACTIVE				0x1
#define X86_UHCI_QH_POOL_NUM(n)			((n & 0x7F) << 1)
#define X86_UHCI_QH_INDEX(n)			((n & 0xFF) << 8)
#define X86_UHCI_QH_TYPE(n)				((n & 0x3) << 16)

#define X86_UHCI_QH_SET_POOL_NUM(n)		((n << 1) & 0xFE)	

/* Pool Definitions */
#define X86_UHCI_POOL_NUM_TD			100
#define X86_UHCI_POOL_NUM_QH			60

#define X86_UHCI_POOL_NULL_TD			0

#define X86_UHCI_POOL_UNSCHEDULE		0
#define X86_UHCI_POOL_ISOCHRONOUS		1
#define X86_UHCI_POOL_ASYNC				9
#define X86_UHCI_POOL_NULL				10

#pragma pack(push, 1)
/* Controller Structure */
typedef struct _UhciController
{
	/* Id */
	uint32_t Id;
	uint32_t HcdId;

	/* Lock */
	Spinlock_t Lock;

	/* Pci Header */
	PciDevice_t *PciDevice;

	/* I/O Registers */
	uint16_t IoBase;

	/* State */
	uint32_t Initialized;

	/* Frame List */
	void *FrameList;
	Addr_t FrameListPhys;

	/* TD Pool */
	UhciTransferDescriptor_t *TdPool[X86_UHCI_POOL_NUM_TD];
	Addr_t TdPoolPhys[X86_UHCI_POOL_NUM_TD];
	Addr_t *TdPoolBuffers[X86_UHCI_POOL_NUM_TD];

	/* QH Pool */
	UhciQueueHead_t *QhPool[X86_UHCI_POOL_NUM_QH];
	Addr_t QhPoolPhys[X86_UHCI_POOL_NUM_QH];

	/* Port Count */
	uint32_t NumPorts;

	/* Transaction List
	* Contains transactions
	* in progress */
	void *TransactionList;

} UhciController_t;
#pragma pack(pop)

/* Prototypes */
_CRT_EXTERN void UhciInit(PciDevice_t *Device);

#endif // !_X86_USB_UHCI_H
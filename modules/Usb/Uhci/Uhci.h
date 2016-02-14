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
* MollenOS USB UHCI Controller Driver
*/

#ifndef _USB_UHCI_H_
#define _USB_UHCI_H_

/* Includes */
#include <Module.h>
#include <DeviceManager.h>

/* Definitions */
#define UHCI_MAX_PORTS				7
#define UHCI_STRUCT_ALIGN			16
#define UHCI_STRUCT_ALIGN_BITS		0xF
#define UHCI_NUM_FRAMES				1024
#define UHCI_FRAME_MASK				2047
#define UHCI_USBLEGEACY				0xC0
#define UHCI_USBRES_INTEL			0xC4

/* Registers */
#define UHCI_REGISTER_COMMAND		0x00
#define UHCI_REGISTER_STATUS		0x02
#define UHCI_REGISTER_INTR			0x04
#define UHCI_REGISTER_FRNUM			0x06
#define UHCI_REGISTER_FRBASEADDR	0x08
#define UHCI_REGISTER_SOFMOD		0x0C
#define UHCI_REGISTER_PORT_BASE		0x10


/* Command bit switches */
#define UHCI_CMD_RUN				0x1
#define UHCI_CMD_HCRESET			0x2
#define UHCI_CMD_GRESET				0x4
#define UHCI_CMD_SUSPENDMODE		0x8
#define UHCI_CMD_GLBRESUME			0x10
#define UHCI_CMD_DEBUG				0x20
#define UHCI_CMD_CONFIGFLAG			0x40
#define UHCI_CMD_MAXPACKET64		0x80

/* Status bit switches */
#define UHCI_STATUS_USBINT			0x1
#define UHCI_STATUS_INTR_ERROR		0x2
#define UHCI_STATUS_RESUME_DETECT	0x4
#define UHCI_STATUS_HOST_SYSERR		0x8
#define UHCI_STATUS_PROCESS_ERR		0x10
#define UHCI_STATUS_HALTED			0x20

/* Interrupt bit switches */
#define UHCI_INTR_TIMEOUT			0x1
#define UHCI_INTR_RESUME			0x2
#define UHCI_INTR_COMPLETION		0x4
#define UHCI_INTR_SHORT_PACKET		0x8

/* Port bit switches */
#define UHCI_PORT_CONNECT_STATUS	0x1
#define UHCI_PORT_CONNECT_EVENT		0x2
#define UHCI_PORT_ENABLED			0x4
#define UHCI_PORT_ENABLED_EVENT		0x8
#define UHCI_PORT_LINE_STATUS		0x30
#define UHCI_PORT_RESUME_DETECT		0x40
#define UHCI_PORT_RESERVED			0x80
#define UHCI_PORT_LOWSPEED			0x100
#define UHCI_PORT_RESET				0x200
#define UHCI_PORT_RESERVED1			0x400
#define UHCI_PORT_RESERVED2			0x800
#define UHCI_PORT_SUSPEND			0x1000

/* Structures */
#define UHCI_STRUCT_ALIGN		16
#define UHCI_STRUCT_ALIGN_BITS	0xF

/* Must be 16 byte aligned */
typedef struct _UhciTransferDescriptor
{
	/* Link Pointer 
	 * Bit 0: If set, end of chain
	 * Bit 1: 1 = QH, 0 = TD.
	 * Bit 2: 1 = Depth, 0 = Breadth.
	 * Bit 3: Must be 0 */
	uint32_t Link;

	/* Control & Status 
	 * Bit 0-10: Actual Length (Bytes Transfered)
	 * Bit 11-15: Reserved
	 * Bit 16: Reserved (1 on most controllers)
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

	/* 4 Reserved dwords 
	 * for software use */

	/* HcdFlags 
	 * Bit 0: Allocation status */
	uint32_t HcdFlags;

	/* Padding */
	uint32_t Padding[3];

} UhciTransferDescriptor_t;

/* Hcd Flags */
#define UHCI_TD_HCD_ALLOCATED			0x1

/* Link bit switches */
#define UHCI_TD_LINK_END				0x1
#define UHCI_TD_LINK_QH					0x2
#define UHCI_TD_LINK_DEPTH				0x4

/* Control / Status bit switches */
#define UHCI_TD_ACTUAL_LENGTH_BITS		0x7FF
#define UHCI_TD_ACTIVE					0x800000
#define UHCI_TD_IOC						0x1000000
#define UHCI_TD_ISOCHRONOUS				0x2000000
#define UHCI_TD_LOWSPEED				0x4000000
#define UHCI_TD_SET_ERR_CNT(n)			((n & 0x3) << 27)
#define UHCI_TD_SHORT_PACKET			(1 << 29)

#define UHCI_TD_ERROR_COUNT(n)			((n >> 27) & 0x3)
#define UHCI_TD_STATUS(n)				((n >> 17) & 0x3F)

/* Header bit switches */
#define UHCI_TD_PID_SETUP			0x2D
#define UHCI_TD_PID_IN				0x69
#define UHCI_TD_PID_OUT				0xE1
#define UHCI_TD_DEVICE_ADDR(n)		((n & 0x7F) << 8)
#define UHCI_TD_EP_ADDR(n)			((n & 0xF) << 15)
#define UHCI_TD_DATA_TOGGLE			(1 << 19)
#define UHCI_TD_MAX_LEN(n)			((n & 0x7FF) << 21)

/* Queue Head, 16 byte align 
 * 8 Bytes used by HC 
 * 24 Bytes used by HCD */
typedef struct _UhciQueueHead
{
	/* Queue Head Link Pointer 
	* Bit 0 - Terminate if set
	* Bit 1 - 1 = QH, 0 = TD */
	uint32_t Link;

	/* Queue Element Link Pointer
	 * Bit 0 - Terminate if set
	 * Bit 1 - 1 = QH, 0 = TD */
	uint32_t Child;


	/* Everything below here is used by HCD
	 * and is not seen by the controller
	 */

	/* Controller Driver Specific 
	 * Bit 0: Allocation status
	 * Bit 1-7: Pool Number
	 * Bit 8-15: Queue Head Index 
	 * Bit 16-17: Queue Head Type (00 Control, 01 Bulk, 10 Interrupt, 11 Isochronous) 
	 * Bit 18: Bandwidth allocated */
	uint32_t Flags;

	/* Virtual Address of next QH */
	uint32_t LinkVirtual;

	/* Virtual Address of TD Head */
	uint32_t ChildVirtual;

	/* Bandwidth Specs */
	uint16_t Phase;
	uint16_t Period;
	uint32_t Bandwidth;

	/* Padding */
	uint32_t Padding[1];

} UhciQueueHead_t;

/* Flag bit switches */
#define UHCI_QH_ACTIVE				0x1
#define UHCI_QH_INDEX(n)			((n & 0xFF) << 8)
#define UHCI_QH_TYPE(n)				((n & 0x3) << 16)
#define UHCI_QH_BANDWIDTH_ALLOC		(1 << 18)

#define UHCI_QH_SET_QUEUE(n)		((n << 1) & 0xFE)	
#define UHCI_QH_CLR_QUEUE(n)		(n & 0xFFFFFF01)
#define UHCI_QT_GET_QUEUE(n)		((n & 0xFE) >> 1)

/* Pool Definitions */
#define UHCI_POOL_NUM_QH			60
#define UHCI_ENDPOINT_MIN_ALLOCATED 25

#define UHCI_POOL_UNSCHEDULE		0
#define UHCI_POOL_ISOCHRONOUS		1
#define UHCI_POOL_ASYNC				9
#define UHCI_POOL_NULL				10
#define UHCI_POOL_START				11

#define UHCI_BANDWIDTH_PHASES		32

/* Endpoint Data */
typedef struct _UhciEndpoint
{
	/* Td's allocated */
	size_t TdsAllocated;

	/* TD Pool */
	UhciTransferDescriptor_t **TDPool;
	Addr_t *TDPoolPhysical;
	Addr_t **TDPoolBuffers;

	/* Lock */
	Spinlock_t Lock;

} UhciEndpoint_t;

#pragma pack(push, 1)
/* Controller Structure */
typedef struct _UhciController
{
	/* Id */
	uint32_t Id;
	int HcdId;

	/* Device */
	MCoreDevice_t *Device;

	/* Lock */
	Spinlock_t Lock;

	/* I/O Registers */
	DeviceIoSpace_t *IoBase;

	/* Frame List */
	void *FrameList;
	Addr_t FrameListPhys;
	uint32_t Frame;

	/* Null Td */
	UhciTransferDescriptor_t *NullTd;
	Addr_t NullTdPhysical;

	/* QH Pool */
	UhciQueueHead_t *QhPool[UHCI_POOL_NUM_QH];
	Addr_t QhPoolPhys[UHCI_POOL_NUM_QH];

	/* Scheduling Loads */
	int Bandwidth[UHCI_BANDWIDTH_PHASES];
	int TotalBandwidth;

	/* Port Count */
	uint32_t NumPorts;

	/* Transaction List
	* Contains transactions
	* in progress */
	void *TransactionList;

} UhciController_t;
#pragma pack(pop)

#endif // !_X86_USB_UHCI_H
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
* MollenOS X86-32 USB OHCI Controller Driver
*/

#ifndef _X86_USB_OHCI_H_
#define _X86_USB_OHCI_H_

/* Includes */
#include <Module.h>
#include <DeviceManager.h>

/* Definitions */
#define X86_OHCI_STRUCT_ALIGN		32
#define X86_OHCI_STRUCT_ALIGN_BITS	0x1F

/* Structures */

/* Must be 16 byte aligned */
#pragma pack(push, 1)
typedef struct _OhciEndpointDescriptor
{
	/* Flags 
	 * Bits 0 - 6: Usb Address of Function
	 * Bits 7 - 10: (Endpoint Nr) Usb Address of this endpoint
	 * Bits 11 - 12: (Direction) Indicates Dataflow, either IN or OUT. (01 -> OUT, 10 -> IN). Otherwise we leave it to the TD descriptor
	 * Bits 13: Speed. 0 indicates full speed, 1 indicates low-speed.
	 * Bits 14: If set, it skips the TD queues and moves on to next EP descriptor
	 * Bits 15: (Format). 0 = Bulk/Control/Interrupt Endpoint. 1 = Isochronous TD format.
	 * Bits 16-26: Maximum Packet Size per data packet.
	 * Bits 27-31: Type -> 0000 (Control), 0001 (Bulk), 0010 (Interrupt) 0011 (Isoc).
	 */
	uint32_t Flags;

	/* TD Queue Tail Pointer
	 * Lower 4 bits not used 
	 * If tail == head, no TD will be used 
	 * We queue to the TAIL, always! */
	uint32_t TailPtr;

	/* TD Queue Head Pointer 
	 * Bits 0: Halted. Indicates that processing of a TD has failed. 
	 * Bits 1: Carry Bit: When a TD is retired this bit is set to last data toggle value.
	 * Bits 2:3 Must be 0. */
	uint32_t HeadPtr;

	/* Next EP Descriptor
	 * Lower 4 bits not used */
	uint32_t NextED;

	/* Next EP Descriptor (Virtual) */
	uint32_t NextEDVirtual;

	/* HCD Defined Data */
	uint32_t HcdData[2];

	/* Bit Flags */
	uint32_t HcdFlags;

} OhciEndpointDescriptor_t;
#pragma pack(pop)

/* Bit Defintions */
#define X86_OHCI_EP_ADDR_BITS		0x7F
#define X86_OHCI_EP_EP_NUM_BITS		0xF
#define X86_OHCI_EP_PACKET_BITS		0x3FF
#define X86_OHCI_EP_EP_NUM(n)		(n << 7)
#define X86_OHCI_EP_PID_OUT			(1 << 11)
#define X86_OHCI_EP_PID_IN			(1 << 12)
#define X86_OHCI_EP_PID_TD			(X86_OHCI_EP_PID_OUT | X86_OHCI_EP_PID_IN)
#define X86_OHCI_EP_LOWSPEED(n)		(n << 13)
#define X86_OHCI_EP_SKIP			0x4000
#define X86_OHCI_EP_ISOCHRONOUS		(1 << 15)
#define X86_OHCI_EP_PACKET_SIZE(n)	(n << 16)
#define X86_OHCI_EP_TYPE(n)			(n << 27)

#define X86_OHCI_ED_ALLOCATED		(1 << 0)

/* Must be 16 byte aligned 
 * General Transfer Descriptor */
#pragma pack(push, 1)
typedef struct _OhciGTransferDescriptor
{
	/* Flags
	 * Bits 0-17:  Available
	 * Bits 18:    If 0, Requires the data to be recieved from an endpoint to exactly fill buffer
	 * Bits 19-20: Direction, 00 = Setup (to ep), 01 = OUT (to ep), 10 = IN (from ep)
	 * Bits 21-23: Interrupt delay count for this TD. This means the HC can delay interrupt a specific amount of frames after TD completion.
	 * Bits 24-25: Data Toggle. It is updated after each successful transmission.
	 * Bits 26-27: Error Count. Updated each transmission that fails. It is 0 on success.
	 * Bits 28-31: Condition Code, if error count is 2 and it fails a third time, this contains error code.
	 * */
	uint32_t Flags;

	/* Current Buffer Pointer 
	 * Size must always be lower than the Maximum Packet Size at the endpoint */
	uint32_t Cbp;

	/* Next TD 
	 * Lower 4 bits must be 0 (aka 16 byte aligned) */
	uint32_t NextTD;

	/* Buffer End 
	 * This is the next 4K page address that can be 
	 * accessed in case of a page boundary crossing 
	 * while using cbp */
	uint32_t BufferEnd;

} OhciGTransferDescriptor_t;
#pragma pack(pop)

/* Transfer Definitions */
#define X86_OHCI_TRANSFER_END_OF_LIST		0x1
#define X86_OHCI_TD_ALLOCATED				(1 << 17)
#define X86_OHCI_TRANSFER_BUF_ROUNDING		(1 << 18)
#define X86_OHCI_TRANSFER_BUF_PID_SETUP		0
#define X86_OHCI_TRANSFER_BUF_PID_OUT		(1 << 19)
#define X86_OHCI_TRANSFER_BUF_PID_IN		(1 << 20)
#define X86_OHCI_TRANSFER_BUF_NO_INTERRUPT	((1 << 21) | (1 << 22) | (1 << 23))
#define X86_OHCI_TRANSFER_BUF_FRAMECOUNT(n)	((n & 0x7) << 24)
#define X86_OHCI_TRANSFER_BUF_TD_TOGGLE		(1 << 25)
#define X86_OHCI_TRANSFER_BUF_NOCC			((1 << 28) | (1 << 29) | (1 << 30) | (1 << 31))

/* Must be 32 byte aligned
 * Isochronous Transfer Descriptor */
typedef struct _OhciITransferDescriptor
{
	/* Flags
	 * Bits 0-15:	Starting Frame of transaction.
	 * Bits 16-20:	Available
	 * Bits 21-23:	Interrupt Delay
	 * Bits 24-26:	Frame Count in this TD, 0 implies 1, and 7 implies 8. (always +1)
	 * Bits 27:		Reserved
	 * Bits 28-31:	Condition Code (Error Code)
	 * */
	uint32_t Flags;

	/* Buffer Page 0 
	 * shall point to first byte of data buffer */
	uint32_t Bp0;

	/* Next TD
	* Lower 4 bits must be 0 (aka 16 byte aligned) */
	uint32_t NextTD;

	/* Buffer End
	* This is the next 4K page address that can be
	* accessed in case of a page boundary crossing
	* while using cbp */
	uint32_t BufferEnd;

	/* Offsets 
	 * Bits 0-11:	Packet Size on IN-transmissions 
	 * Bits 12:		CrossPage Field
	 * Bits 12-15:	Condition Code (Error Code) */
	uint16_t Offsets[8];

} OhciITransferDescriptor_t;

/* Host Controller Communcations Area 
 * must be 256-byte aligned */
typedef struct _OhciHCCA
{
	/* Interrupt Table 
	 * 32 pointers to interrupt ed's */
	uint32_t InterruptTable[32];

	/* Current Frame Number */
	uint16_t CurrentFrame;

	/* It is 0 */
	uint16_t Pad1;

	/* Which head is done, it gets set to this pointer at end of frame
	 * and generates an interrupt */
	uint32_t HeadDone;

	/* Reserved for HC */
	uint8_t Reserved[116];

} OhciHCCA_t;

/* Register Space */
typedef struct _OhciRegisters
{
	uint32_t	HcRevision;
	uint32_t	HcControl;
	uint32_t	HcCommandStatus;
	uint32_t	HcInterruptStatus;
	uint32_t	HcInterruptEnable;
	uint32_t	HcInterruptDisable;

	uint32_t	HcHCCA;
	uint32_t	HcPeriodCurrentED;
	uint32_t	HcControlHeadED;
	uint32_t	HcControlCurrentED;
	uint32_t	HcBulkHeadED;
	uint32_t	HcBulkCurrentED;
	uint32_t	HcDoneHead;

	uint32_t	HcFmInterval;
	uint32_t	HcFmRemaining;
	uint32_t	HcFmNumber;
	uint32_t	HcPeriodicStart;
	uint32_t	HcLSThreshold;

	/* Max of 15 ports */
	uint32_t	HcRhDescriptorA;
	uint32_t	HcRhDescriptorB;
	uint32_t	HcRhStatus;
	uint32_t	HcRhPortStatus[15];

} OhciRegisters_t;

/* Bit Defintions */
#define X86_OHCI_REVISION			0x10

#define X86_OHCI_CMD_RESETCTRL		(1 << 0)
#define X86_OHCI_CMD_TDACTIVE_CTRL	(1 << 1)
#define X86_OHCI_CMD_TDACTIVE_BULK	(1 << 2)
#define X86_OHCI_CMD_OWNERSHIP		(1 << 3)

#define X86_OHCI_CTRL_ENABLE_QUEUES	0x3C

#define X86_OHCI_CTRL_USB_RESET		0x0
#define X86_OHCI_CTRL_USB_RESUME	0x40
#define X86_OHCI_CTRL_USB_WORKING	0x80
#define X86_OHCI_CTRL_USB_SUSPEND	0xC0

#define X86_OHCI_FI					0x2edf
#define X86_OHCI_FI_MASK			0x3fff
#define X86_OHCI_GETFSMP(fi)		((fi >> 16) & 0x7FFF)
#define X86_OHCI_FSMP(fi)			(0x7fff & ((6 * ((fi) - 210)) / 7))

/* Bits 0 and 1 */
#define X86_OHCI_CTRL_SRATIO_BITS		(1 << 0) | (1 << 1)

/* Bit 2-5 (Queues) */
#define X86_OCHI_CTRL_PERIODIC_LIST		(1 << 2)
#define X86_OHCI_CTRL_ISOCHRONOUS_LIST	(1 << 3)
#define X86_OHCI_CTRL_CONTROL_LIST		(1 << 4)
#define X86_OHCI_CTRL_BULK_LIST			(1 << 5)
#define X86_OHCI_CTRL_ALL_LISTS			(1 << 2) | (1 << 3) | (1 << 4) | (1 << 5)

/* Bits 6 and 7 */
#define X86_OHCI_CTRL_FSTATE_BITS		(1 << 6) | (1 << 7) 

/* Bits 8 */
#define X86_OHCI_CTRL_INT_ROUTING		(1 << 8)

/* Bit 10 */
#define X86_OHCI_CTRL_REMOTE_WAKE		(1 << 10)

#define X86_OHCI_INTR_SCHEDULING_OVRRN	0x1
#define X86_OHCI_INTR_HEAD_DONE			0x2
#define X86_OHCI_INTR_SOF				0x4
#define X86_OHCI_INTR_RESUME_DETECT		0x8
#define X86_OHCI_INTR_FATAL_ERROR		0x10
#define X86_OHCI_INTR_FRAME_OVERFLOW	0x20
#define X86_OHCI_INTR_ROOT_HUB_EVENT	0x40
#define X86_OHCI_INTR_OWNERSHIP_EVENT	0x40000000
#define X86_OHCI_INTR_MASTER_INTR		0x80000000

#define X86_OHCI_MAX_PACKET_SIZE_BITS	0x7FFF0000
#define X86_OHCI_FRMV_FRT				(1 << 31)

#define X86_OHCI_DESCA_DEVICE_TYPE		(1 << 10)

#define X86_OHCI_STATUS_POWER_ON		(1 << 16)

#define X86_OHCI_PORT_DISABLE			0x1
#define X86_OHCI_PORT_CONNECTED			0x1
#define X86_OHCI_PORT_ENABLED			(1 << 1)
#define X86_OHCI_PORT_SUSPENDED			(1 << 2)
#define X86_OHCI_PORT_OVER_CURRENT		(1 << 3)
#define X86_OHCI_PORT_RESET				0x10
#define X86_OHCI_PORT_POWER_ENABLE		0x100
#define X86_OHCI_PORT_LOW_SPEED			(1 << 9)
#define X86_OHCI_PORT_CONNECT_EVENT		0x10000 /* Connect / Disconnect event */
#define X86_OHCI_PORT_ENABLE_EVENT		(1 << 17)
#define X86_OHCI_PORT_SUSPEND_EVENT		(1 << 18)
#define X86_OHCI_PORT_OVR_CURRENT_EVENT	(1 << 19)
#define X86_OHCI_PORT_RESET_EVENT		(1 << 20)

/* Pool Definitions */
#define X86_OHCI_POOL_CONTROL_EDS		25
#define X86_OHCI_POOL_BULK_EDS			49
#define X86_OHCI_POOL_NUM_ED			50
#define OHCI_ENDPOINT_MIN_ALLOCATED		25

/* Endpoint Data */
typedef struct _OhciEndpoint
{
	/* Td's allocated */
	size_t TdsAllocated;

	/* TD Pool */
	OhciGTransferDescriptor_t **TDPool;
	Addr_t *TDPoolPhysical;
	Addr_t **TDPoolBuffers;

	/* Lock */
	Spinlock_t Lock;

} OhciEndpoint_t;

/* Interrupt Table */
typedef struct _OhciIntrTable
{
	/* 32 Ep's */
	OhciEndpointDescriptor_t Ms16[16];
	OhciEndpointDescriptor_t Ms8[8];
	OhciEndpointDescriptor_t Ms4[4];
	OhciEndpointDescriptor_t Ms2[2];
	OhciEndpointDescriptor_t Ms1[1];
	OhciEndpointDescriptor_t Stop;

} OhciIntrTable_t;

/* Controller Structure */
typedef struct _OhciController
{
	/* Id */
	uint32_t Id;
	uint32_t HcdId;

	/* Device */
	MCoreDevice_t *Device;

	/* Lock */
	Spinlock_t Lock;

	/* Register Space (Physical) */
	uint32_t HccaSpace;

	/* Registers */
	volatile OhciRegisters_t *Registers;
	volatile OhciHCCA_t *HCCA;

	/* ED Pool */
	OhciEndpointDescriptor_t *EDPool[X86_OHCI_POOL_NUM_ED];
	OhciGTransferDescriptor_t *NullTd;

	/* Interrupt Table & List */
	OhciIntrTable_t *IntrTable;
	OhciEndpointDescriptor_t *ED32[32];
	uint32_t I32;
	uint32_t I16;
	uint32_t I8;
	uint32_t I4;
	uint32_t I2;

	/* Power */
	uint32_t PowerMode;
	uint32_t PowerOnDelayMs;

	/* Port Count */
	uint32_t Ports;

	/* Transaction Queue */
	uint32_t TransactionsWaitingControl;
	uint32_t TransactionsWaitingBulk;
	Addr_t TransactionQueueControl;
	Addr_t TransactionQueueBulk;

	/* Transaction List 
	 * Contains transactions
	 * in progress */
	void *TransactionList;

} OhciController_t;

/* Power Mode Flags */
#define X86_OHCI_POWER_ALWAYS_ON		0
#define X86_OHCI_POWER_PORT_CONTROLLED	1
#define X86_OHCI_POWER_PORT_GLOBAL		2

#endif // !_X86_USB_OHCI_H_

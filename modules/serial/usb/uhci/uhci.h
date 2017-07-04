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
 * MollenOS MCore - Universal Host Controller Interface Driver
 * Todo:
 * Power Management
 * Finish the FSBR implementation, right now there is no guarantee of order ls/fs/bul
 * The isochronous unlink/link needs improvements, it does not support multiple isocs in same frame 
 */

#ifndef _USB_UHCI_H_
#define _USB_UHCI_H_

/* Includes
 * - Library */
#include <os/driver/contracts/usbhost.h>
#include <os/osdefs.h>
#include <ds/list.h>

#include "../common/manager.h"
#include "../common/scheduler.h"

//#define UHCI_FSBR

/* UHCI Definitions 
 * Definitions and constants used in general for the controller setup */
#define UHCI_MAX_PORTS					7
#define UHCI_NUM_FRAMES					1024
#define UHCI_FRAME_MASK					2047
#define UHCI_USBLEGEACY					0xC0
#define UHCI_USBRES_INTEL				0xC4

#define UHCI_STRUCT_ALIGN				16
#define UHCI_STRUCT_ALIGN_BITS			0xF

/* UHCI Register Definitions
 * A list of all the fixed-offsets registers that exist in the io-space of
 * the uhci-controller. */
#define UHCI_REGISTER_COMMAND			0x00
#define UHCI_REGISTER_STATUS			0x02
#define UHCI_REGISTER_INTR				0x04
#define UHCI_REGISTER_FRNUM				0x06
#define UHCI_REGISTER_FRBASEADDR		0x08
#define UHCI_REGISTER_SOFMOD			0x0C
#define UHCI_REGISTER_PORT_BASE			0x10

/* UHCI Register Definitions
 * Bit definitions for the Command register */
#define UHCI_COMMAND_RUN				0x1
#define UHCI_COMMAND_HCRESET			0x2
#define UHCI_COMMAND_GRESET				0x4
#define UHCI_COMMAND_SUSPENDMODE		0x8
#define UHCI_COMMAND_GLBRESUME			0x10
#define UHCI_COMMAND_DEBUG				0x20
#define UHCI_COMMAND_CONFIGFLAG			0x40
#define UHCI_COMMAND_MAXPACKET64		0x80

/* UHCI Register Definitions
 * Bit definitions for the Status register */
#define UHCI_STATUS_USBINT				0x1
#define UHCI_STATUS_INTR_ERROR			0x2
#define UHCI_STATUS_RESUME_DETECT		0x4
#define UHCI_STATUS_HOST_SYSERR			0x8
#define UHCI_STATUS_PROCESS_ERR			0x10
#define UHCI_STATUS_HALTED				0x20

/* UHCI Register Definitions
 * Bit definitions for the Interrupt register */
#define UHCI_INTR_TIMEOUT				0x1
#define UHCI_INTR_RESUME				0x2
#define UHCI_INTR_COMPLETION			0x4
#define UHCI_INTR_SHORT_PACKET			0x8

/* UHCI Register Definitions
 * Bit definitions for the Port register */
#define UHCI_PORT_CONNECT_STATUS		0x1
#define UHCI_PORT_CONNECT_EVENT			0x2
#define UHCI_PORT_ENABLED				0x4
#define UHCI_PORT_ENABLED_EVENT			0x8
#define UHCI_PORT_LINE_STATUS			0x30
#define UHCI_PORT_RESUME_DETECT			0x40
#define UHCI_PORT_RESERVED				0x80
#define UHCI_PORT_LOWSPEED				0x100
#define UHCI_PORT_RESET					0x200
#define UHCI_PORT_RESERVED1				0x400
#define UHCI_PORT_RESERVED2				0x800
#define UHCI_PORT_SUSPEND				0x1000

/* UhciTransferDescriptor
 * Describes a generic transfer-descriptor that can be either of all
 * four different transaction types. Must be 16 byte aligned. */
PACKED_TYPESTRUCT(UhciTransferDescriptor, {
	uint32_t					Link;
	uint32_t 					Flags;
	uint32_t 					Header;
	uint32_t 					Buffer;

	// 16 Byte software meta-data
	uint32_t 					HcdFlags;
	uint32_t 					PhysicalAddress;
	uint32_t 					Frame;
	uint32_t 					Unused;
});

/* UhciTransferDescriptor::Link & UhciQueueHead::Link,Child
 * Contains definitions and bitfield definitions for UhciTransferDescriptor::Link */
#define UHCI_LINK_END					0x1
#define UHCI_LINK_QH					0x2		// 1 => Qh, 0 => Td
#define UHCI_LINK_DEPTH					0x4		// 1 => Depth, 0 => Breadth

/* UhciTransferDescriptor::HcdFlags
 * Contains definitions and bitfield definitions for UhciTransferDescriptor::HcdFlags */
#define UHCI_TD_ALLOCATED				0x1

/* UhciTransferDescriptor::Flags
 * Contains definitions and bitfield definitions for UhciTransferDescriptor::Flags */
#define UHCI_TD_LENGTH_MASK				0x7FF
#define UHCI_TD_ACTIVE					0x800000
#define UHCI_TD_IOC						0x1000000
#define UHCI_TD_ISOCHRONOUS				0x2000000
#define UHCI_TD_LOWSPEED				0x4000000
#define UHCI_TD_SETCOUNT(n)				((n & 0x3) << 27)
#define UHCI_TD_SHORT_PACKET			(1 << 29)
#define UHCI_TD_ACTUALLENGTH(n)			(n & UHCI_TD_LENGTH_MASK)

#define UHCI_TD_GETCOUNT(n)				((n >> 27) & 0x3)
#define UHCI_TD_STATUS(n)				((n >> 17) & 0x3F)

/* UhciTransferDescriptor::Header
 * Contains definitions and bitfield definitions for UhciTransferDescriptor::Header */
#define UHCI_TD_PID_SETUP				0x2D
#define UHCI_TD_PID_IN					0x69
#define UHCI_TD_PID_OUT					0xE1
#define UHCI_TD_DEVICE_ADDR(n)			((n & 0x7F) << 8)
#define UHCI_TD_EP_ADDR(n)				((n & 0xF) << 15)
#define UHCI_TD_DATA_TOGGLE				(1 << 19)
#define UHCI_TD_MAX_LEN(n)				((n & UHCI_TD_LENGTH_MASK) << 21)
#define UHCI_TD_GET_LEN(n)				((n >> 21) & UHCI_TD_LENGTH_MASK)

/* Queue Head, 16 byte align 
 * 8 Bytes used by HC 
 * 24 Bytes used by HCD */
PACKED_TYPESTRUCT(UhciQueueHead, {
	uint32_t 					Link;
	uint32_t 					Child;

	// 24 Byte software meta-data
	uint32_t 					Flags;
	uint32_t 					LinkVirtual;	// Virtual address of Link
	uint32_t 					ChildVirtual;	// Virtual address of Child
	uint16_t 					Phase;
	uint16_t					Period;
	uint32_t 					Bandwidth;
	uint32_t 					Unused;
});

/* UhciQueueHead::Flags
 * Contains definitions and bitfield definitions for UhciQueueHead::Flags
 * Bit 0: Allocation status
 * Bit 1-7: Pool Number
 * Bit 8-15: Queue Head Index 
 * Bit 16-17: Queue Head Type (00 Control, 01 Bulk, 10 Interrupt, 11 Isochronous) 
 * Bit 18: Bandwidth allocated 
 * Bit 19: FSBR 
 * Bit 20: Unschedule */
#define UHCI_QH_ACTIVE				0x1
#define UHCI_QH_INDEX(n)			((n & 0xFF) << 8)
#define UHCI_QH_TYPE(n)				((n & 0x3) << 16)
#define UHCI_QH_BANDWIDTH_ALLOC		(1 << 18)
#define UHCI_QH_FSBR				(1 << 19)
#define UHCI_QH_UNSCHEDULE			(1 << 20)

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

#define UHCI_POOL_LCTRL				11
#define UHCI_POOL_FCTRL				12
#define UHCI_POOL_FBULK				13
#define UHCI_POOL_FSBR				UHCI_POOL_FCTRL

/* Where shared alloc starts */
#define UHCI_POOL_START				14
#define UHCI_BANDWIDTH_PHASES		32

/* UhciControl
 * Contains all necessary Queue related information
 * and information needed to schedule */
typedef struct _UhciControl {
	// Resources
	UhciQueueHead_t			   **QHPool;
	UhciTransferDescriptor_t   **TDPool;
	uintptr_t					 QHPoolPhysical;
	uintptr_t					 TDPoolPhysical;

	// Frames
	void*						 FrameList;
	uintptr_t 					 FrameListPhysical;
	size_t 					 	 Frame;

	// Bandwidth
	int							 Bandwidth[UHCI_BANDWIDTH_PHASES];
	int							 TotalBandwidth;

	// Transactions
	List_t						*TransactionList;
} UhciControl_t;

/* UhciController 
 * Contains all per-controller information that is
 * needed to control, queue and handle devices on an uhci-controller. */
typedef struct _UhciController {
	UsbManagerController_t	 Base;
	UhciControl_t			 QueueControl;
} UhciController_t;

/* UhciRead16
 * Reads a 2-byte value from the control-space of the controller */
__EXTERN
uint16_t
UhciRead16(
	_In_ UhciController_t *Controller, 
	_In_ uint16_t Register);

/* UhciRead32
 * Reads a 4-byte value from the control-space of the controller */
__EXTERN
uint32_t
UhciRead32(
	_In_ UhciController_t *Controller, 
	_In_ uint16_t Register);

/* UhciWrite8
 * Writes a single byte value to the control-space of the controller */
__EXTERN
void
UhciWrite8(
	_In_ UhciController_t *Controller, 
	_In_ uint16_t Register, 
	_In_ uint8_t Value);

/* UhciWrite16
 * Writes a 2-byte value to the control-space of the controller */
__EXTERN
void
UhciWrite16(
	_In_ UhciController_t *Controller, 
	_In_ uint16_t Register, 
	_In_ uint16_t Value);

/* UhciWrite32
 * Writes a 4-byte value to the control-space of the controller */
__EXTERN
void 
UhciWrite32(
	_In_ UhciController_t *Controller, 
	_In_ uint16_t Register, 
	_In_ uint32_t Value);

/* UhciStart
 * Boots the controller, if it succeeds OsSuccess is returned. */
__EXTERN
OsStatus_t
UhciStart(
	_In_ UhciController_t *Controller);

/* UhciStop
 * Stops the controller, if it succeeds OsSuccess is returned. */
__EXTERN
OsStatus_t
UhciStop(
	_In_ UhciController_t *Controller);

/* UhciControllerCreate 
 * Initializes and creates a new Uhci Controller instance
 * from a given new system device on the bus. */
__EXTERN
UhciController_t*
UhciControllerCreate(
	_In_ MCoreDevice_t *Device);

/* UhciControllerDestroy
 * Destroys an existing controller instance and cleans up
 * any resources related to it */
__EXTERN
OsStatus_t
UhciControllerDestroy(
	_In_ UhciController_t *Controller);

/* UhciPortPrepare
 * Resets the port and also clears out any event on the port line. */
__EXTERN
OsStatus_t
UhciPortPrepare(
	_In_ UhciController_t *Controller, 
	_In_ int Index);

/* UhciPortGetStatus 
 * Retrieve the current port status, with connected and enabled information */
__EXTERN
void
UhciPortGetStatus(
	_In_ UhciController_t *Controller,
	_In_ int Index,
	_Out_ UsbHcPortDescriptor_t *Port);

/* UsbQueueTransferGeneric 
 * Queues a new transfer for the given driver
 * and pipe. They must exist. The function does not block*/
__EXTERN
OsStatus_t
UsbQueueTransferGeneric(
	_InOut_ UsbManagerTransfer_t *Transfer);

/* UsbDequeueTransferGeneric 
 * Removes a queued transfer from the controller's framelist */
__EXTERN
OsStatus_t
UsbDequeueTransferGeneric(
	_In_ UsbManagerTransfer_t *Transfer);

#endif // !_USB_UHCI_H_

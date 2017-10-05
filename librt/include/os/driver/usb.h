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
 * MollenOS MCore - Usb Definitions & Structures
 * - This header describes the base usb-structure, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#ifndef _USB_INTERFACE_H_
#define _USB_INTERFACE_H_

/* Includes
 * - Library */
#include <os/osdefs.h>

/* Includes
 * - System */
#include <os/driver/usb/definitions.h>
#include <os/driver/bufferpool.h>

/* UsbControllerType
 * Describes the possible types of usb controllers */
typedef enum _UsbControllerType {
	UsbOHCI,
	UsbUHCI,
	UsbEHCI,
	UsbXHCI
} UsbControllerType_t;

/* UsbSpeed 
 * Describes the possible speeds for usb devices */
typedef enum _UsbSpeed {
	LowSpeed,       // 1.0 / 1.1
	FullSpeed,      // 1.0 / 1.1 / 2.0 (HID)
	HighSpeed,      // 2.0
	SuperSpeed      // 3.0
} UsbSpeed_t;

/* UsbHcPortDescriptor 
 * Describes the current port information */
PACKED_TYPESTRUCT(UsbHcPortDescriptor, {
	UsbSpeed_t                          Speed;
	int                                 Enabled;
	int                                 Connected;
});

/* UsbHcEndpointDescriptor 
 * Describes a generic endpoint for an usb device */
PACKED_TYPESTRUCT(UsbHcEndpointDescriptor, {
	UsbEndpointType_t                   Type;
	UsbEndpointSynchronization_t        Synchronization;
	size_t                              Address;
	int                                 Direction;
	size_t                              MaxPacketSize;
	size_t                              Bandwidth;
	size_t                              Interval;
});

/* UsbHcInterfaceVersion 
 * Describes a version of an interface and it's endpoint count. */
PACKED_TYPESTRUCT(UsbHcInterfaceVersion, {
	int                                 Id;
	int                                 EndpointCount;
});

/* UsbHcInterface 
 * Describes a generic interface for an usb device. There can be 
 * multiple versions each with a number of different endpoints. */
PACKED_TYPESTRUCT(UsbHcInterface, {
	int                                 Id;
	size_t                              Class;
	size_t                              Subclass;
	size_t                              Protocol;
	size_t                              StringIndex;
	int	                                VersionCount;
	UsbHcInterfaceVersion_t             Versions[USB_MAX_VERSIONS];
});

/* UsbHcDevice 
 * Describes a device present on a port of a usb-host controller. 
 * A device then further has a bunch of interfaces, and those interfaces
 * have a bunch of endpoints. */
PACKED_TYPESTRUCT(UsbHcDevice, {
    int                         Address;
    UsbSpeed_t                  Speed;
	int                         InterfaceCount;
	int                         LanguageCount;

	// Information
	uint8_t                     Class;
	uint8_t                     Subclass;
	uint8_t                     Protocol;
	uint16_t                    VendorId;
	uint16_t                    ProductId;

	// Configurations
	uint8_t                     ConfigurationCount;
	uint16_t                    ConfigMaxLength;
	uint16_t                    MaxPowerConsumption;
	uint16_t                    MaxPacketSize;
	uint8_t                     Configuration;

	// String indexes
	uint8_t                     StringIndexProduct;
	uint8_t                     StringIndexManufactor;
	uint8_t                     StringIndexSerialNumber;

	// Integrated buffers
	uint16_t                    Languages[USB_MAX_LANGUAGES];
});

/* Bit-fields and definitions for field UsbHcEndpointDescriptor::Direction
 * Defined below */
#define USB_ENDPOINT_IN					0x0
#define USB_ENDPOINT_OUT				0x1
#define USB_ENDPOINT_BOTH				0x2

/* UsbTransactionType 
 * Describes the possible types of usb transactions */
typedef enum _UsbTransactionType {
	SetupTransaction,
	InTransaction,
	OutTransaction
} UsbTransactionType_t;

/* UsbTransferType 
 * Describes the type of transfer, it can be one of 4 that describe
 * either Control, Bulk, Interrupt or Isochronous */
typedef enum _UsbTransferType {
	ControlTransfer,
	BulkTransfer,
	InterruptTransfer,
	IsochronousTransfer
} UsbTransferType_t;

/* UsbTransferStatus
 * Describes a unified way of reporting how a transfer ended.
 * Where the initial state is NotProcessed */
typedef enum _UsbTransferStatus {
	TransferNotProcessed,
	TransferFinished,
	TransferStalled,
	TransferNotResponding,
	TransferInvalidToggles,
	TransferInvalidData,
	TransferNAK,
	TransferBabble
} UsbTransferStatus_t;

/* UsbTransaction
 * Describes a single transaction in an usb-transfer operation */
PACKED_TYPESTRUCT(UsbTransaction, {
	UsbTransactionType_t				Type;
	int									Handshake;	// ACK always end in 1
	int									ZeroLength;

	// Data Information
	uintptr_t							BufferAddress;
	size_t								Length;
});

/* UsbTransfer 
 * Describes an usb-transfer, that consists of transfer information
 * and a bunch of transactions. */
PACKED_TYPESTRUCT(UsbTransfer, {
    // Generic Information
    Flags_t                             Flags;
    UUId_t                              Pipe;
	UsbTransferType_t                   Type;
	UsbSpeed_t                          Speed;
	size_t                              Length;
    UsbTransaction_t                    Transactions[3];
    int                                 TransactionCount;

	// Endpoint Information
	UsbHcEndpointDescriptor_t           Endpoint;

	// Periodic Information
	int	                                UpdatesOn;
    __CONST void*                       PeriodicData;
    size_t                              PeriodicBufferSize;
});

/* UsbTransfer::Flags
 * Bit-definitions and declarations for the field. */
#define USB_TRANSFER_SHORT_NOT_OK       0x00000001

/* UsbTransferResult
 * Describes the result of an usb-transfer */
PACKED_TYPESTRUCT(UsbTransferResult, {
	// Generic Information
	UUId_t					Id;
	size_t					BytesTransferred;
	UsbTransferStatus_t 	Status;
});

/* UsbInitialize
 * Initializes libusb and enables the use of all the control
 * functions that require a shared buffer-pool. */
__EXTERN
OsStatus_t
UsbInitialize(void);

/* UsbCleanup
 * Frees the shared resources allocated by UsbInitialize. */
__EXTERN
OsStatus_t
UsbCleanup(void);

/* UsbRetrievePool 
 * Retrieves the shared usb-memory pool for transfers. Only
 * use this for small short-term use buffers. */
__EXTERN
BufferPool_t*
UsbRetrievePool(void);

/* UsbTransferInitialize
 * Initializes the usb-transfer structure from the given
 * device and requested transfer type. */
__EXTERN
OsStatus_t
UsbTransferInitialize(
    _InOut_ UsbTransfer_t *Transfer,
    _In_ UsbHcDevice_t *Device,
    _In_ UsbHcEndpointDescriptor_t *Endpoint,
    _In_ UsbTransferType_t Type);

/* UsbTransferSetup 
 * Initializes a transfer for a control setup-transaction. 
 * If there is no data-stage then set Data members to 0. */
__EXTERN
OsStatus_t
UsbTransferSetup(
    _InOut_ UsbTransfer_t *Transfer,
    _In_ uintptr_t SetupAddress,
    _In_ uintptr_t DataAddress,
    _In_ size_t DataLength,
    _In_ UsbTransactionType_t DataType);

/* UsbTransferPeriodic
 * Initializes a transfer for a periodic-transaction. */
__EXTERN
OsStatus_t
UsbTransferPeriodic(
    _InOut_ UsbTransfer_t *Transfer,
    _In_ uintptr_t BufferAddress,
    _In_ size_t BufferLength,
    _In_ size_t DataLength,
    _In_ UsbTransactionType_t DataDirection,
    _In_ int Notify,
    _In_ __CONST void *NotifyData);

/* UsbTransferIn 
 * Creates an In-transaction in the given usb-transfer. Both buffer and length 
 * must be pre-allocated - and passed here. If handshake == 1 it's an ack-transaction. */
__EXTERN
OsStatus_t
UsbTransferIn(
	_InOut_ UsbTransfer_t *Transfer,
	_In_ uintptr_t BufferAddress, 
	_In_ size_t Length,
    _In_ int Handshake);
    
/* UsbTransferOut 
 * Creates an Out-transaction in the given usb-transfer. Both buffer and length 
 * must be pre-allocated - and passed here. If handshake == 1 it's an ack-transaction. */
__EXTERN
OsStatus_t
UsbTransferOut(
	_Out_ UsbTransfer_t *Transfer,
	_In_ uintptr_t BufferAddress, 
	_In_ size_t Length,
	_In_ int Handshake);

/* UsbTransferQueue 
 * Queues a new Control or Bulk transfer for the given driver
 * and pipe. They must exist. The function blocks untill execution */
__EXTERN
OsStatus_t
UsbTransferQueue(
	_In_ UUId_t Driver,
	_In_ UUId_t Device,
	_In_ UsbTransfer_t *Transfer,
	_Out_ UsbTransferResult_t *Result);

/* UsbTransferQueuePeriodic 
 * Queues a new Interrupt or Isochronous transfer. This transfer is 
 * persistant untill device is disconnected or Dequeue is called. */
__EXTERN
OsStatus_t
UsbTransferQueuePeriodic(
	_In_ UUId_t Driver,
	_In_ UUId_t Device,
	_In_ UsbTransfer_t *Transfer,
	_Out_ UUId_t *TransferId);

/* UsbTransferDequeuePeriodic 
 * Dequeues an existing periodic transfer from the given controller. The transfer
 * and the controller must be valid. */
__EXTERN
OsStatus_t
UsbTransferDequeuePeriodic(
	_In_ UUId_t Driver,
	_In_ UUId_t Device,
	_In_ UUId_t TransferId);

/* UsbHostResetPort
 * Resets the given port on the given controller and queries it's
 * status afterwards. This returns an updated status of the port after
 * the reset. */
__EXTERN
OsStatus_t
UsbHostResetPort(
	_In_ UUId_t Driver,
	_In_ UUId_t Device,
	_In_ int Index,
	_Out_ UsbHcPortDescriptor_t *Descriptor);

/* UsbHostQueryPort 
 * Queries the port-descriptor of host-controller port. */
__EXTERN
OsStatus_t
UsbHostQueryPort(
	_In_ UUId_t Driver,
	_In_ UUId_t Device,
	_In_ int Index,
	_Out_ UsbHcPortDescriptor_t *Descriptor);

/* UsbSetAddress
 * Changes the address of the usb-device. This permanently updates the address. 
 * It is not possible to change the address once enumeration is done. */
__EXTERN
UsbTransferStatus_t
UsbSetAddress(
	_In_ UUId_t Driver,
	_In_ UUId_t Device,
	_In_ UsbHcDevice_t *UsbDevice, 
	_In_ UsbHcEndpointDescriptor_t *Endpoint, 
    _In_ int Address);
    
/* UsbGetDeviceDescriptor
 * Queries the device descriptor of an usb device on a given port. */
__EXTERN
UsbTransferStatus_t
UsbGetDeviceDescriptor(
	_In_ UUId_t Driver,
	_In_ UUId_t Device,
	_In_ UsbHcDevice_t *UsbDevice, 
    _In_ UsbHcEndpointDescriptor_t *Endpoint,
    _Out_ UsbDeviceDescriptor_t *DeviceDescriptor);

/* UsbGetConfigDescriptor
 * Queries the configuration descriptor. Ideally this function is called twice to get
 * the full configuration descriptor. Once to retrieve the actual descriptor, and then
 * twice to retrieve the full descriptor with all information. */
__EXTERN
UsbTransferStatus_t
UsbGetConfigDescriptor(
	_In_ UUId_t Driver,
	_In_ UUId_t Device,
	_In_ UsbHcDevice_t *UsbDevice, 
    _In_ UsbHcEndpointDescriptor_t *Endpoint,
    _Out_ void *ConfigDescriptorBuffer,
    _Out_ size_t ConfigDescriptorBufferLength);

/* UsbSetConfiguration
 * Updates the configuration of an usb-device. This changes active endpoints. */
__EXTERN
UsbTransferStatus_t
UsbSetConfiguration(
	_In_ UUId_t Driver,
	_In_ UUId_t Device,
	_In_ UsbHcDevice_t *UsbDevice, 
    _In_ UsbHcEndpointDescriptor_t *Endpoint,
    _In_ int Configuration);
    
/* UsbClearFeature
 * Indicates to an usb-device that we want to request a feature/state disabled. */
__EXTERN
UsbTransferStatus_t
UsbClearFeature(
    _In_ UUId_t Driver,
    _In_ UUId_t Device,
    _In_ UsbHcDevice_t *UsbDevice, 
    _In_ UsbHcEndpointDescriptor_t *Endpoint,
    _In_ uint8_t Target, 
    _In_ uint16_t Index, 
    _In_ uint16_t Feature);

/* UsbSetFeature
 * Indicates to an usb-device that we want to request a feature/state enabled. */
__EXTERN
UsbTransferStatus_t
UsbSetFeature(
	_In_ UUId_t Driver,
	_In_ UUId_t Device,
	_In_ UsbHcDevice_t *UsbDevice, 
    _In_ UsbHcEndpointDescriptor_t *Endpoint,
	_In_ uint8_t Target, 
	_In_ uint16_t Index, 
    _In_ uint16_t Feature);
    
/* UsbGetStringLanguages
 * Gets the device string language descriptors (Index 0). The retrieved string descriptors are
 * stored in the given descriptor storage. */
__EXTERN
UsbTransferStatus_t
UsbGetStringLanguages(
	_In_ UUId_t Driver,
	_In_ UUId_t Device,
	_In_ UsbHcDevice_t *UsbDevice, 
    _In_ UsbHcEndpointDescriptor_t *Endpoint,
    _Out_ UsbStringDescriptor_t *StringDescriptor);

/* UsbGetStringDescriptor
 * Queries the usb device for a string with the given language and index. 
 * The provided buffer must be of size at-least 64 bytes. */
__EXTERN
UsbTransferStatus_t
UsbGetStringDescriptor(
    _In_ UUId_t Driver,
    _In_ UUId_t Device,
    _In_ UsbHcDevice_t *UsbDevice, 
    _In_ UsbHcEndpointDescriptor_t *Endpoint,
    _In_ size_t LanguageId, 
    _In_ size_t StringIndex, 
    _Out_ char *String);

/* UsbExecutePacket
 * Executes a custom packet with or without a data-stage. Use this for vendor-specific
 * control requests. */
__EXTERN
UsbTransferStatus_t
UsbExecutePacket(
    _In_ UUId_t Driver,
    _In_ UUId_t Device,
    _In_ UsbHcDevice_t *UsbDevice, 
    _In_ UsbHcEndpointDescriptor_t *Endpoint,
    _In_ uint8_t Direction,
    _In_ uint8_t Type,
    _In_ uint8_t ValueHigh,
    _In_ uint8_t ValueLow,
    _In_ uint16_t Index,
    _In_ uint16_t Length,
    _Out_ void *Buffer);

/* UsbEndpointReset
 * Resets the data for the given endpoint. This includes the data-toggles. 
 * This function is unavailable for control-endpoints. */
__EXTERN
OsStatus_t
UsbEndpointReset(
    _In_ UUId_t Driver,
    _In_ UUId_t Device,
    _In_ UsbHcDevice_t *UsbDevice, 
    _In_ UsbHcEndpointDescriptor_t *Endpoint);

/* UsbQueryControllers 
 * Queries the available usb controllers and their status in the system
 * The given array must be of size USB_MAX_CONTROLLERS. */

/* UsbQueryPorts 
 * Queries the available ports and their status on the given usb controller
 * the given array must be of size USB_MAX_PORTS. */

/* UsbQueryPipes 
 * Queries the available interfaces and endpoints on a given
 * port and controller. Querying with NULL pointers returns the count
 * otherwise fills the array given with information */

 /* UsbQueryDescriptor
  * Queries a common usb-descriptor from the given usb port and 
  * usb controller. The given array is filled with the descriptor information */
 
#endif //!_USB_INTERFACE_H_

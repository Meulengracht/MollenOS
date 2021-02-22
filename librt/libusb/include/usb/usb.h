/**
 * MollenOS
 *
 * Copyright 2017, Philip Meulengracht
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
 * Usb Definitions & Structures
 * - This header describes the base usb-structure, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#ifndef _USB_INTERFACE_H_
#define _USB_INTERFACE_H_

#include <ddk/ddkdefs.h>
#include <ddk/bufferpool.h>
#include <ddk/busdevice.h>
#include <usb/definitions.h>
#include <ddk/service.h>

DECL_STRUCT(UsbDevice);

/* USB Definitions
 * Contains magic constants, settings and bit definitions */
#define USB_DEVICE_CLASS     0x0000CABB
#define USB_TRANSACTIONCOUNT 3

typedef enum UsbTransferStatus {
    // HCD Error Codes
    TransferCreated,
    TransferQueued,
    TransferInProgress,
    TransferFinished,
    TransferInvalid,
    TransferNoBandwidth,

    // Transaction Error Codes
	TransferStalled,
	TransferNotResponding,
	TransferInvalidToggles,
    TransferBufferError,
	TransferNAK,
	TransferBabble
} UsbTransferStatus_t;

typedef enum UsbControllerType {
	UsbOHCI,
	UsbUHCI,
	UsbEHCI,
	UsbXHCI
} UsbControllerType_t;

#define USB_SPEED_LOW        0  // 1.0 / 1.1
#define USB_SPEED_FULL       1  // 1.0 / 1.1 / 2.0 (HID)
#define USB_SPEED_HIGH       2  // 2.0
#define USB_SPEED_SUPER      3  // 3.0
#define USB_SPEED_SUPER_PLUS 4  // 4.0

/* UsbHcPortDescriptor 
 * Describes the current port information */
typedef struct UsbHcPortDescriptor {
	uint8_t Speed;
	int     Enabled;
	int     Connected;
} UsbHcPortDescriptor_t;

/* UsbHcController
 * Describes a generic controller with information needed
 * in order for the manager to function */
typedef struct UsbHcController {
    BusDevice_t           Device;
    UsbControllerType_t   Type;
    UsbHcPortDescriptor_t Ports[USB_MAX_PORTS];
} UsbHcController_t;

typedef struct usb_address {
    uint8_t HubAddress;
    uint8_t PortAddress;
    uint8_t DeviceAddress;
    uint8_t EndpointAddress;
} UsbHcAddress_t;

typedef struct usb_device_interface_setting {
	usb_interface_descriptor_t base;
	usb_endpoint_descriptor_t* endpoints;
} usb_device_interface_setting_t;

typedef struct usb_device_interface {
	usb_device_interface_setting_t* settings;
	int                             settings_count;
} usb_device_interface_t;

typedef struct usb_device_configuration {
	usb_config_descriptor_t base;
	usb_device_interface_t* interfaces;
} usb_device_configuration_t;

typedef struct usb_device_context {
	UUId_t   controller_device_id;
	UUId_t   controller_driver_id;
	UUId_t   hub_device_id;
	UUId_t   hub_driver_id;
    uint8_t  hub_address;
    uint8_t  port_address;
    uint8_t  device_address;
    uint16_t configuration_length;
    uint16_t device_mps;
    uint8_t  speed;
} usb_device_context_t;

typedef struct usb_transaction {
	uint8_t Type;
	uint8_t Flags;
	UUId_t  BufferHandle;
	size_t  BufferOffset;
	size_t  Length;
} usb_transaction_t;

// Value definitions for UsbTransaction::Type
#define USB_TRANSACTION_SETUP 0
#define USB_TRANSACTION_IN    1
#define USB_TRANSACTION_OUT   2

// Bit definitions for UsbTransaction::Flags
#define USB_TRANSACTION_ZLP       0x01
#define USB_TRANSACTION_HANDSHAKE 0x02

typedef struct usb_transfer {
    UsbHcAddress_t    Address;
	uint8_t           Type;
	uint8_t           Speed;
	uint16_t          MaxPacketSize;
	uint8_t           Flags;
	uint8_t           TransactionCount;
    usb_transaction_t Transactions[USB_TRANSACTIONCOUNT];

	// Periodic Information
    const void* PeriodicData;
    uint32_t    PeriodicBufferSize;
    uint8_t     PeriodicBandwith;
    uint8_t     PeriodicInterval;
} UsbTransfer_t;

// Value definitions for UsbTransfer::Type
#define USB_TRANSFER_CONTROL     0
#define USB_TRANSFER_BULK        1
#define USB_TRANSFER_INTERRUPT   2
#define USB_TRANSFER_ISOCHRONOUS 3

/* UsbTransfer::Flags
 * Bit-definitions and declarations for the field. */
#define USB_TRANSFER_NO_NOTIFICATION 0x01
#define USB_TRANSFER_SHORT_NOT_OK    0x02

#define USB_TRANSFER_ENDPOINT_CONTROL NULL

__EXTERN OsStatus_t       UsbInitialize(void);
__EXTERN OsStatus_t       UsbCleanup(void);
__EXTERN struct dma_pool* UsbRetrievePool(void);

/**
 * Initializes the usb-transfer to target the device and endpoint.
 * @param transfer A pointer to the transfer structure.
 * @param device   A pointer to the device context.
 * @param endpoint A pointer to an endpoint structure, or USB_TRANSFER_ENDPOINT_CONTROL.
 * @param type     The type of transfer, CONTROL/BULK/INTERRUPT/ISOC.
 * @param flags    Configuration flags for the transfer.
 */
__EXTERN void
UsbTransferInitialize(
    _In_ UsbTransfer_t*             transfer,
    _In_ usb_device_context_t*      device,
    _In_ usb_endpoint_descriptor_t* endpoint,
    _In_ uint8_t                    type,
    _In_ uint8_t                    flags);

/**
 * Initializes a transfer for a control setup-transaction.
 * If there is no data-stage then set Data members to 0.
 * @param transfer
 * @param setupBufferHandle
 * @param setupBufferOffset
 * @param dataBufferHandle
 * @param dataBufferOffset
 * @param dataLength
 * @param type
 */
__EXTERN void
UsbTransferSetup(
    _In_ UsbTransfer_t* transfer,
    _In_ UUId_t         setupBufferHandle,
    _In_ size_t         setupBufferOffset,
    _In_ UUId_t         dataBufferHandle,
    _In_ size_t         dataBufferOffset,
    _In_ size_t         dataLength,
    _In_ uint8_t        type);

/**
 * Initializes a transfer for a periodic-transaction.
 * @param Transfer
 * @param BufferHandle
 * @param BufferOffset
 * @param BufferLength
 * @param DataLength
 * @param DataDirection
 * @param NotifificationData
 */
__EXTERN void
UsbTransferPeriodic(
    _In_ UsbTransfer_t* Transfer,
    _In_ UUId_t         BufferHandle,
    _In_ size_t         BufferOffset,
    _In_ size_t         BufferLength,
    _In_ size_t         DataLength,
    _In_ uint8_t        DataDirection,
    _In_ const void*    NotifificationData);

/**
 * Creates an In-transaction in the given usb-transfer. Both buffer and length
 * must be pre-allocated - and passed here. If handshake == 1 it's an ack-transaction.
 * @param Transfer
 * @param BufferHandle
 * @param BufferOffset
 * @param Length
 * @param Handshake
 * @return
 */
__EXTERN OsStatus_t
UsbTransferIn(
	_In_ UsbTransfer_t* Transfer,
    _In_ UUId_t         BufferHandle,
    _In_ size_t         BufferOffset,
	_In_ size_t         Length,
    _In_ int            Handshake);

/**
 * Creates an Out-transaction in the given usb-transfer. Both buffer and length
 * must be pre-allocated - and passed here. If handshake == 1 it's an ack-transaction.
 * @param Transfer
 * @param BufferHandle
 * @param BufferOffset
 * @param Length
 * @param Handshake
 * @return
 */
__EXTERN OsStatus_t
UsbTransferOut(
	_In_ UsbTransfer_t* Transfer,
    _In_ UUId_t         BufferHandle,
    _In_ size_t         BufferOffset,
	_In_ size_t         Length,
    _In_ int            Handshake);

/**
 * Queues a new Control or Bulk transfer for the given driver
 * and pipe. They must exist. The function blocks untill execution.
 * Status-code must be TransferQueued on success.
 * @param deviceContext
 * @param transfer
 * @param bytesTransferred
 * @return
 */
__EXTERN UsbTransferStatus_t
UsbTransferQueue(
    _In_  usb_device_context_t* deviceContext,
	_In_  UsbTransfer_t*        transfer,
	_Out_ size_t*               bytesTransferred);

/**
 * Queues a new Interrupt or Isochronous transfer. This transfer is
 * persistant untill device is disconnected or Dequeue is called.
 * Returns TransferFinished on success.
 * @param deviceContext
 * @param transfer
 * @param transferIdOut
 * @return
 */
__EXTERN UsbTransferStatus_t
UsbTransferQueuePeriodic(
    _In_  usb_device_context_t* deviceContext,
	_In_  UsbTransfer_t*        transfer,
	_Out_ UUId_t*               transferIdOut);

/**
 * Can be used to reset an interrupt or isochronous transfer after a stall condition has occurred and been cleared.
 * @param deviceContext
 * @param transferId
 * @return
 */
__EXTERN OsStatus_t
UsbTransferResetPeriodic(
        _In_ usb_device_context_t* deviceContext,
        _In_ UUId_t                transferId);

/**
 * Dequeues an existing periodic transfer from the given controller. The transfer
 * and the controller must be valid. Returns TransferFinished on success.
 * @param deviceContext
 * @param transferId
 * @return
 */
__EXTERN OsStatus_t
UsbTransferDequeuePeriodic(
    _In_ usb_device_context_t* deviceContext,
	_In_ UUId_t                transferId);

/**
 * Resets the given port on the given hub and queries it's
 * status afterwards. This returns an updated status of the port after
 * the reset.
 * @param hubDriverId
 * @param deviceId
 * @param portAddress
 * @param portDescriptor
 * @return
 */
__EXTERN OsStatus_t
UsbHubResetPort(
	_In_ UUId_t                 hubDriverId,
	_In_ UUId_t                 deviceId,
	_In_ uint8_t                portAddress,
	_In_ UsbHcPortDescriptor_t* portDescriptor);

/**
 * Queries the port-descriptor of host-controller port.
 * @param hubDriverId
 * @param deviceId
 * @param portAddress
 * @param portDescriptor
 * @return
 */
__EXTERN OsStatus_t
UsbHubQueryPort(
	_In_ UUId_t                 hubDriverId,
	_In_ UUId_t                 deviceId,
	_In_ uint8_t                portAddress,
	_In_ UsbHcPortDescriptor_t* portDescriptor);

/**
 * UsbSetAddress
 * * Changes the address of the usb-device. This permanently updates the address. 
 * * It is not possible to change the address once enumeration is done.
 */
__EXTERN UsbTransferStatus_t
UsbSetAddress(
	_In_ usb_device_context_t* deviceContext,
    _In_ int                   address);

/**
 * UsbGetDeviceDescriptor
 * * Queries the device descriptor of an usb device on a given port.
 */
__EXTERN UsbTransferStatus_t
UsbGetDeviceDescriptor(
	_In_ usb_device_context_t*    deviceContext,
    _In_ usb_device_descriptor_t* deviceDescriptor);

/* UsbGetConfigDescriptor
 * Queries the configuration descriptor. Ideally this function is called twice to get
 * the full configuration descriptor. Once to retrieve the actual descriptor, and then
 * twice to retrieve the full descriptor with all information. */
__EXTERN UsbTransferStatus_t
UsbGetConfigDescriptor(
	_In_ usb_device_context_t*       deviceContext,
	_In_ int                         configurationIndex,
    _In_ usb_device_configuration_t* configuration);

__EXTERN UsbTransferStatus_t
UsbGetActiveConfigDescriptor(
	_In_ usb_device_context_t*       deviceContext,
    _In_ usb_device_configuration_t* configuration);

__EXTERN void
UsbFreeConfigDescriptor(
    _In_ usb_device_configuration_t* configuration);

/* UsbSetConfiguration
 * Updates the configuration of an usb-device. This changes active endpoints. */
__EXTERN UsbTransferStatus_t
UsbSetConfiguration(
	_In_ usb_device_context_t* deviceContext,
    _In_ int                   configurationIndex);
    
/* UsbGetStringLanguages
 * Gets the device string language descriptors (Index 0). The retrieved string descriptors are
 * stored in the given descriptor storage. */
__EXTERN UsbTransferStatus_t
UsbGetStringLanguages(
	_In_ usb_device_context_t*    deviceContext,
    _In_ usb_string_descriptor_t* descriptor);

/* UsbGetStringDescriptor
 * Queries the usb device for a string with the given language and index. 
 * The provided buffer must be of size at-least 64 bytes. */
__EXTERN UsbTransferStatus_t
UsbGetStringDescriptor(
	_In_ usb_device_context_t* deviceContext,
    _In_  size_t               LanguageId, 
    _In_  size_t               StringIndex, 
    _Out_ char*                String);

/* UsbClearFeature
 * Indicates to an usb-device that we want to request a feature/state disabled. */
__EXTERN UsbTransferStatus_t
UsbClearFeature(
	_In_ usb_device_context_t* deviceContext,
    _In_ uint8_t               Target, 
    _In_ uint16_t              Index, 
    _In_ uint16_t              Feature);

/* UsbSetFeature
 * Indicates to an usb-device that we want to request a feature/state enabled. */
__EXTERN UsbTransferStatus_t
UsbSetFeature(
	_In_ usb_device_context_t* deviceContext,
	_In_ uint8_t               Target, 
	_In_ uint16_t              Index, 
    _In_ uint16_t              Feature);

/* UsbExecutePacket
 * Executes a custom packet with or without a data-stage. Use this for vendor-specific
 * control requests. */
__EXTERN UsbTransferStatus_t
UsbExecutePacket(
	_In_ usb_device_context_t* deviceContext,
    _In_ uint8_t               Direction,
    _In_ uint8_t               Type,
    _In_ uint8_t               ValueLow,
    _In_ uint8_t               ValueHigh,
    _In_ uint16_t              Index,
    _In_ uint16_t              Length,
    _In_ void*                 Buffer);

/* UsbEndpointReset
 * Resets the data for the given endpoint. This includes the data-toggles. 
 * This function is unavailable for control-endpoints. */
__EXTERN OsStatus_t
UsbEndpointReset(
	_In_ usb_device_context_t* deviceContext, 
    _In_ uint8_t               endpointAddress);

/**
 * Registers a new controller and root hub with the usb stack
 * @param device
 * @param type
 * @param portCount
 * @return
 */
DDKDECL(OsStatus_t,
UsbControllerRegister(
    _In_ Device_t*           device,
    _In_ UsbControllerType_t type,
    _In_ int                 portCount));

/**
 * Removes a controller from the usb stack and any device associated with it
 * @param DeviceId
 * @return
 */
DDKDECL(OsStatus_t,
UsbControllerUnregister(
    _In_ UUId_t deviceId));

/**
 *
 * @param usbDevice
 * @param portCount
 * @return
 */
DDKDECL(OsStatus_t,
UsbHubRegister(
        _In_ UsbDevice_t* usbDevice,
        _In_ int          portCount));

/**
 *
 * @param deviceId
 * @return
 */
DDKDECL(OsStatus_t,
UsbHubUnregister(
        _In_ UUId_t deviceId));

/**
 * Call this event when any port status update needs action taken by the usb stack
 * @param DeviceId
 * @param PortAddress
 * @return
 */
DDKDECL(OsStatus_t,
UsbEventPort(
    _In_ UUId_t  DeviceId,
    _In_ uint8_t PortAddress));

/**
 *
 * @param deviceId
 * @param portAddress
 * @return
 */
DDKDECL(OsStatus_t,
UsbPortError(
        _In_ UUId_t  deviceId,
        _In_ uint8_t portAddress));

/* UsbQueryControllerCount
 * Queries the available number of usb controllers. */
DDKDECL(OsStatus_t,
UsbQueryControllerCount(
    _Out_ int* ControllerCount));

/* UsbQueryController
 * Queries the controller with the given index. Index-max is
 * the controller count - 1. */
DDKDECL(OsStatus_t,
UsbQueryController(
    _In_ int                Index,
    _In_ UsbHcController_t* Controller));

/* UsbQueryPipes 
 * Queries the available interfaces and endpoints on a given
 * port and controller. Querying with NULL pointers returns the count
 * otherwise fills the array given with information */

 /* UsbQueryDescriptor
  * Queries a common usb-descriptor from the given usb port and 
  * usb controller. The given array is filled with the descriptor information */
 
#endif //!_USB_INTERFACE_H_

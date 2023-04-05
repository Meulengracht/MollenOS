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
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
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
#include <ddk/service.h>
#include <ds/mstring.h>
#include <usb/definitions.h>

DECL_STRUCT(UsbDevice);

/* USB Definitions
 * Contains magic constants, settings and bit definitions */
#define USB_DEVICE_CLASS     0x0000CABB
#define USB_TRANSACTIONCOUNT 3

enum USBTransferCode {
    // HCD Error Codes
    TransferOK,
    TransferInvalid,
    TransferNoBandwidth,

    // Transaction Error Codes
	TransferStalled,
	TransferNotResponding,
	TransferInvalidToggles,
    TransferBufferError,
	TransferNAK,
	TransferBabble
};

enum USBControllerKind {
    USBCONTROLLER_KIND_UHCI,
    USBCONTROLLER_KIND_OHCI,
    USBCONTROLLER_KIND_EHCI,
    USBCONTROLLER_KIND_XHCI
};

enum USBSpeed {
    USBSPEED_LOW,       // 1.0 / 1.1
    USBSPEED_FULL,      // 1.0 / 1.1 / 2.0 (HID)
    USBSPEED_HIGH,      // 2.0
    USBSPEED_SUPER,     // 3.0
    USBSPEED_SUPER_PLUS // 4.0
};

/**
 * @brief Represents a USB controller port.
 */
typedef struct USBPortDescriptor {
    enum USBSpeed Speed;
	int           Enabled;
	int           Connected;
} USBPortDescriptor_t;

/**
 * @brief Describes the characteristics of an USB controller device.
 */
typedef struct USBControllerDevice {
    BusDevice_t            Device;
    enum USBControllerKind Kind;
    USBPortDescriptor_t    Ports[USB_MAX_PORTS];
} USBControllerDevice_t;

/**
 * @brief Describes an absolute USB address for a USB packet.
 */
typedef struct USBAddress {
    uint8_t HubAddress;
    uint8_t PortAddress;
    uint8_t DeviceAddress;
    uint8_t EndpointAddress;
} USBAddress_t;

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
	uuid_t   controller_device_id;
	uuid_t   controller_driver_id;
	uuid_t   hub_device_id;
	uuid_t   hub_driver_id;
    uint8_t  hub_address;
    uint8_t  port_address;
    uint8_t  device_address;
    uint16_t configuration_length;
    uint16_t device_mps;
    uint8_t  speed;
} usb_device_context_t;

enum USBTransactionType {
    USB_TRANSACTION_SETUP,
    USB_TRANSACTION_IN,
    USB_TRANSACTION_OUT
};

typedef struct USBTransaction {
    enum USBTransactionType Type;
	unsigned int            Flags;
	uuid_t                  BufferHandle;
	uint32_t                BufferOffset;
	uint32_t                Length;
} USBTransaction_t;

// Bit definitions for UsbTransaction::Flags
#define USB_TRANSACTION_ZLP       0x01
#define USB_TRANSACTION_HANDSHAKE 0x02

enum USBTransferType {
    USBTRANSFER_TYPE_CONTROL,
    USBTRANSFER_TYPE_BULK,
    USBTRANSFER_TYPE_INTERRUPT,
    USBTRANSFER_TYPE_ISOC,
};

typedef struct USBTransfer {
    USBAddress_t         Address;
    enum USBTransferType Type;
	uint8_t              Speed;
	uint16_t             MaxPacketSize;
	uint16_t             Flags;
	uint8_t              TransactionCount;
    USBTransaction_t     Transactions[USB_TRANSACTIONCOUNT];

	// Periodic Information
    const void* PeriodicData;
    uint32_t    PeriodicBufferSize;
    uint8_t     PeriodicBandwith;
    uint8_t     PeriodicInterval;
} USBTransfer_t;

/* UsbTransfer::Flags
 * Bit-definitions and declarations for the field. */
#define USB_TRANSFER_NO_NOTIFICATION 0x01
#define USB_TRANSFER_SHORT_NOT_OK    0x02

#define USB_TRANSFER_ENDPOINT_CONTROL NULL

extern oserr_t          UsbInitialize(void);
extern void             UsbCleanup(void);
extern struct dma_pool* UsbRetrievePool(void);

/**
 * Initializes the usb-transfer to target the device and endpoint.
 * @param transfer A pointer to the transfer structure.
 * @param device   A pointer to the device context.
 * @param endpoint A pointer to an endpoint structure, or USB_TRANSFER_ENDPOINT_CONTROL.
 * @param type     The type of transfer, CONTROL/BULK/INTERRUPT/ISOC.
 * @param flags    Configuration flags for the transfer.
 */
extern void
UsbTransferInitialize(
        _In_ USBTransfer_t*             transfer,
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
extern void
UsbTransferSetup(
        _In_ USBTransfer_t* transfer,
        _In_ uuid_t         setupBufferHandle,
        _In_ size_t         setupBufferOffset,
        _In_ uuid_t         dataBufferHandle,
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
extern void
UsbTransferPeriodic(
        _In_ USBTransfer_t* Transfer,
        _In_ uuid_t         BufferHandle,
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
extern oserr_t
UsbTransferIn(
        _In_ USBTransfer_t* Transfer,
        _In_ uuid_t         BufferHandle,
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
extern oserr_t
UsbTransferOut(
        _In_ USBTransfer_t* Transfer,
        _In_ uuid_t         BufferHandle,
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
extern enum USBTransferCode
UsbTransferQueue(
        _In_  usb_device_context_t* deviceContext,
        _In_  USBTransfer_t*        transfer,
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
extern enum USBTransferCode
UsbTransferQueuePeriodic(
        _In_  usb_device_context_t* deviceContext,
        _In_  USBTransfer_t*        transfer,
        _Out_ uuid_t*               transferIdOut);

/**
 * Can be used to reset an interrupt or isochronous transfer after a stall condition has occurred and been cleared.
 * @param deviceContext
 * @param transferId
 * @return
 */
extern oserr_t
UsbTransferResetPeriodic(
        _In_ usb_device_context_t* deviceContext,
        _In_ uuid_t                transferId);

/**
 * Dequeues an existing periodic transfer from the given controller. The transfer
 * and the controller must be valid. Returns TransferFinished on success.
 * @param deviceContext
 * @param transferId
 * @return
 */
extern oserr_t
UsbTransferDequeuePeriodic(
        _In_ usb_device_context_t* deviceContext,
        _In_ uuid_t                transferId);

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
extern oserr_t
UsbHubResetPort(
        _In_ uuid_t                 hubDriverId,
        _In_ uuid_t                 deviceId,
        _In_ uint8_t                portAddress,
        _In_ USBPortDescriptor_t* portDescriptor);

/**
 * Queries the port-descriptor of host-controller port.
 * @param hubDriverId
 * @param deviceId
 * @param portAddress
 * @param portDescriptor
 * @return
 */
extern oserr_t
UsbHubQueryPort(
        _In_ uuid_t                 hubDriverId,
        _In_ uuid_t                 deviceId,
        _In_ uint8_t                portAddress,
        _In_ USBPortDescriptor_t* portDescriptor);

/**
 * UsbSetAddress
 * * Changes the address of the usb-device. This permanently updates the address. 
 * * It is not possible to change the address once enumeration is done.
 */
extern enum USBTransferCode
UsbSetAddress(
	_In_ usb_device_context_t* deviceContext,
    _In_ int                   address);

/**
 * UsbGetDeviceDescriptor
 * * Queries the device descriptor of an usb device on a given port.
 */
extern enum USBTransferCode
UsbGetDeviceDescriptor(
	_In_ usb_device_context_t*    deviceContext,
    _In_ usb_device_descriptor_t* deviceDescriptor);

/* UsbGetConfigDescriptor
 * Queries the configuration descriptor. Ideally this function is called twice to get
 * the full configuration descriptor. Once to retrieve the actual descriptor, and then
 * twice to retrieve the full descriptor with all information. */
extern enum USBTransferCode
UsbGetConfigDescriptor(
	_In_ usb_device_context_t*       deviceContext,
	_In_ int                         configurationIndex,
    _In_ usb_device_configuration_t* configuration);

extern enum USBTransferCode
UsbGetActiveConfigDescriptor(
	_In_ usb_device_context_t*       deviceContext,
    _In_ usb_device_configuration_t* configuration);

extern void
UsbFreeConfigDescriptor(
    _In_ usb_device_configuration_t* configuration);

/* UsbSetConfiguration
 * Updates the configuration of an usb-device. This changes active endpoints. */
extern enum USBTransferCode
UsbSetConfiguration(
	_In_ usb_device_context_t* deviceContext,
    _In_ int                   configurationIndex);
    
/* UsbGetStringLanguages
 * Gets the device string language descriptors (Index 0). The retrieved string descriptors are
 * stored in the given descriptor storage. */
extern enum USBTransferCode
UsbGetStringLanguages(
	_In_ usb_device_context_t*    deviceContext,
    _In_ usb_string_descriptor_t* descriptor);

/**
 * @brief Retrieves the string on the device with the given index. The string is
 * returned as an mstring. LanguageID 0 requests a list of languages.
 */
extern enum USBTransferCode
UsbGetStringDescriptor(
	_In_  usb_device_context_t* deviceContext,
    _In_  size_t                languageId,
    _In_  size_t                stringIndex,
    _Out_ mstring_t**           stringOut);

/* UsbClearFeature
 * Indicates to an usb-device that we want to request a feature/state disabled. */
extern enum USBTransferCode
UsbClearFeature(
	_In_ usb_device_context_t* deviceContext,
    _In_ uint8_t               Target, 
    _In_ uint16_t              Index, 
    _In_ uint16_t              Feature);

/* UsbSetFeature
 * Indicates to an usb-device that we want to request a feature/state enabled. */
extern enum USBTransferCode
UsbSetFeature(
	_In_ usb_device_context_t* deviceContext,
	_In_ uint8_t               Target, 
	_In_ uint16_t              Index, 
    _In_ uint16_t              Feature);

/* UsbExecutePacket
 * Executes a custom packet with or without a data-stage. Use this for vendor-specific
 * control requests. */
extern enum USBTransferCode
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
extern oserr_t
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
DDKDECL(oserr_t,
UsbControllerRegister(
    _In_ Device_t*              device,
    _In_ enum USBControllerKind kind,
    _In_ int                    portCount));

/**
 * Removes a controller from the usb stack and any device associated with it
 * @param DeviceID the device ID of the controller to unregister
 */
DDKDECL(void,
UsbControllerUnregister(
        _In_ uuid_t deviceID));

/**
 *
 * @param usbDevice
 * @param portCount
 * @return
 */
DDKDECL(oserr_t,
UsbHubRegister(
        _In_ UsbDevice_t* usbDevice,
        _In_ int          portCount));

/**
 *
 * @param deviceId
 * @return
 */
DDKDECL(oserr_t,
UsbHubUnregister(
        _In_ uuid_t deviceId));

/**
 * Call this event when any port status update needs action taken by the usb stack
 * @param DeviceId
 * @param PortAddress
 * @return
 */
DDKDECL(oserr_t,
UsbEventPort(
    _In_ uuid_t  DeviceId,
    _In_ uint8_t PortAddress));

/**
 *
 * @param deviceId
 * @param portAddress
 * @return
 */
DDKDECL(oserr_t,
UsbPortError(
        _In_ uuid_t  deviceId,
        _In_ uint8_t portAddress));

/* UsbQueryControllerCount
 * Queries the available number of usb controllers. */
DDKDECL(oserr_t,
UsbQueryControllerCount(
    _Out_ int* ControllerCount));

/* UsbQueryController
 * Queries the controller with the given index. Index-max is
 * the controller count - 1. */
DDKDECL(oserr_t,
UsbQueryController(
    _In_ int                Index,
    _In_ USBControllerDevice_t* Controller));

/* UsbQueryPipes 
 * Queries the available interfaces and endpoints on a given
 * port and controller. Querying with NULL pointers returns the count
 * otherwise fills the array given with information */

 /* UsbQueryDescriptor
  * Queries a common usb-descriptor from the given usb port and 
  * usb controller. The given array is filled with the descriptor information */
 
#endif //!_USB_INTERFACE_H_

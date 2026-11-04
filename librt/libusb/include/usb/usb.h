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
 *
 *
 * USB Definitions and Structures
 * Common types and functions for USB devices and transfers.
 */

#ifndef _USB_INTERFACE_H_
#define _USB_INTERFACE_H_

#include <ddk/ddkdefs.h>
#include <ddk/bufferpool.h>
#include <ddk/devices/bus.h>
#include <ddk/service.h>
#include <ds/mstring.h>
#include <usb/definitions.h>

DECL_STRUCT(UsbDevice);

/* USB constants and flags. */
#define USB_DEVICE_CLASS     0x0000CABB
#define USB_TRANSACTIONCOUNT 3

enum USBTransferCode {
    USBTRANSFERCODE_INVALID,

    // HCD Error Codes
    USBTRANSFERCODE_SUCCESS,
    USBTRANSFERCODE_CANCELLED,
    USBTRANSFERCODE_NOBANDWIDTH,

    // Transaction Error Codes
    USBTRANSFERCODE_STALL,
    USBTRANSFERCODE_NORESPONSE,
    USBTRANSFERCODE_DATATOGGLEMISMATCH,
    USBTRANSFERCODE_BUFFERERROR,
    USBTRANSFERCODE_NAK,
    USBTRANSFERCODE_BABBLE // CRC/Bitstuffing
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

enum USBTransferDirection {
    USBTRANSFER_DIRECTION_IN,
    USBTRANSFER_DIRECTION_OUT
};

enum USBTransferType {
    USBTRANSFER_TYPE_CONTROL,
    USBTRANSFER_TYPE_BULK,
    USBTRANSFER_TYPE_INTERRUPT,
    USBTRANSFER_TYPE_ISOC,
};

/**
 * @brief Connection state and speed of a USB controller port.
 */
typedef struct USBPortDescriptor {
    enum USBSpeed Speed;
	int           Enabled;
	int           Connected;
} USBPortDescriptor_t;

/**
 * @brief USB controller type, bus device and port states.
 */
typedef struct USBControllerDevice {
    BusDevice_t            Device;
    enum USBControllerKind Kind;
    USBPortDescriptor_t    Ports[USB_MAX_PORTS];
} USBControllerDevice_t;

/**
 * @brief Hub, port, device and endpoint addresses for a USB packet.
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

typedef struct USBTransfer {
    USBAddress_t              Address;
    enum USBTransferType      Type;
    enum USBTransferDirection Direction;
    enum USBSpeed             Speed;
	uint16_t                  MaxPacketSize;
	uint16_t                  Flags;
    uuid_t                    BufferHandle;
    uint32_t                  BufferOffset;
    uint32_t                  Length;
    uint8_t                   PeriodicBandwith;
    uint8_t                   PeriodicInterval;
} USBTransfer_t;

/**
 * USBTransfer_t::Flags bits.
 */
#define USB_TRANSFER_NO_NOTIFICATION 0x01
#define USB_TRANSFER_SHORT_NOT_OK    0x02

#define USB_TRANSFER_ENDPOINT_CONTROL NULL

extern oserr_t          UsbInitialize(void);
extern void             UsbCleanup(void);
extern struct dma_pool* UsbRetrievePool(void);

/**
 * @brief Initialize the fields common to all USB transfers.
 *
 * @param transfer
 * @param device
 * @param endpoint
 * @param type
 * @param direction
 * @param flags
 * @param dataBufferHandle
 * @param dataBufferOffset
 * @param dataLength
 */
extern void
UsbTransferInitialize(
        _In_ USBTransfer_t*             transfer,
        _In_ usb_device_context_t*      device,
        _In_ usb_endpoint_descriptor_t* endpoint,
        _In_ enum USBTransferType       type,
        _In_ enum USBTransferDirection  direction,
        _In_ unsigned int               flags,
        _In_ uuid_t                     dataBufferHandle,
        _In_ size_t                     dataBufferOffset,
        _In_ size_t                     dataLength);

/**
 * Queues a new Control or Bulk transfer for the given driver
 * and pipe. They must exist. The function blocks untill execution.
 * Status-code must be TransferQueued on success.
 * @param deviceContext
 * @param transfer
 * @param bytesTransferred
 * @return
 */
extern oserr_t
UsbTransferQueue(
        _In_  usb_device_context_t* deviceContext,
        _In_  USBTransfer_t*        transfer,
        _Out_ enum USBTransferCode* transferResultOut,
        _Out_ size_t*               bytesTransferredOut);

/**
 * Queues a new Interrupt or Isochronous transfer. This transfer is
 * persistant untill device is disconnected or Dequeue is called.
 * Returns TransferFinished on success.
 * @param deviceContext
 * @param transfer
 * @param transferIdOut
 * @return
 */
extern oserr_t
UsbTransferQueuePeriodic(
        _In_  usb_device_context_t* deviceContext,
        _In_  USBTransfer_t*        transfer,
        _Out_ uuid_t*               transferIdOut);

/**
 * @brief Reset an interrupt or isochronous transfer after clearing its stall.
 *
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
 * @brief Reset a hub port and return its status after the reset.
 *
 * @param hubDriverId
 * @param deviceId
 * @param portAddress
 * @param portDescriptor
 * @return
 */
extern oserr_t
UsbHubResetPort(
        _In_ uuid_t               hubDriverId,
        _In_ uuid_t               deviceId,
        _In_ uint8_t              portAddress,
        _In_ USBPortDescriptor_t* portDescriptor);

/**
 * @brief Query the status of a hub port.
 *
 * @param hubDriverId
 * @param deviceId
 * @param portAddress
 * @param portDescriptor
 * @return
 */
extern oserr_t
UsbHubQueryPort(
        _In_ uuid_t               hubDriverId,
        _In_ uuid_t               deviceId,
        _In_ uint8_t              portAddress,
        _In_ USBPortDescriptor_t* portDescriptor);

/**
 * @brief Disable a hub port before allowing another device to use address zero.
 *
 * @param hubDriverId Driver serving the hub.
 * @param deviceId Hub device identifier.
 * @param portAddress Port index on the hub, using the same numbering as reset.
 * @return OS_EOK when the port is disabled, otherwise a transport or device error.
 */
__EXTERN oserr_t
UsbHubDisablePort(
    _In_ uuid_t  hubDriverId,
    _In_ uuid_t  deviceId,
    _In_ uint8_t portAddress);

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
        _In_ uint8_t               direction,
        _In_ uint8_t               type,
        _In_ uint8_t               valueLow,
        _In_ uint8_t               valueHigh,
        _In_ uint16_t              index,
        _In_ uint16_t              length,
        _In_ void*                 buffer);

/* UsbEndpointReset
 * Resets the data for the given endpoint. This includes the data-toggles. 
 * This function is unavailable for control-endpoints. */
extern oserr_t
UsbEndpointReset(
	_In_ usb_device_context_t* deviceContext, 
    _In_ uint8_t               endpointAddress);

/**
 * @brief Register a controller and its root hub with the USB stack.
 *
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
 * @brief Remove a controller and its devices from the USB stack.
 *
 * @param DeviceID the device ID of the controller to unregister
 */
DDKDECL(void,
UsbControllerUnregister(
        _In_ uuid_t deviceID));

/**
 * @brief Register a hub and wait until its controller-side configuration completes.
 *
 * @param usbDevice Enumerated USB hub device.
 * @param portCount Number of downstream ports.
 * @param characteristics Hub descriptor characteristics.
 * @return OS_EOK when port operations may begin, otherwise a registration error.
 */
DDKDECL(oserr_t,
UsbHubRegister(
    _In_ UsbDevice_t* usbDevice,
    _In_ int          portCount,
    _In_ uint16_t     characteristics));

/**
 *
 * @param deviceId
 * @return
 */
DDKDECL(oserr_t,
UsbHubUnregister(
        _In_ uuid_t deviceId));

/**
 * @brief Notify the USB stack when a port status change needs to be handled.
 *
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
    _In_ int                    Index,
    _In_ USBControllerDevice_t* Controller));

/* UsbQueryPipes 
 * Queries the available interfaces and endpoints on a given
 * port and controller. Querying with NULL pointers returns the count
 * otherwise fills the array given with information */

 /* UsbQueryDescriptor
  * Queries a common usb-descriptor from the given usb port and 
  * usb controller. The given array is filled with the descriptor information */


/**
 * @brief Detach a USB device from the given controller.
 * 
 * @param controllerId Controller device ID to send in the detach request.
 * @param device USB device context containing the controller driver ID.
 * @return None.
 */
DDKDECL(void,
UsbDetachDevice(
    _In_ uuid_t                controllerId,
    _In_ usb_device_context_t* device));

#endif //!_USB_INTERFACE_H_

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
* MollenOS USB Core Driver
*/

#ifndef __MODULE_USBCORE__
#define __MODULE_USBCORE__

/* Includes */
#include <crtdefs.h>
#include <stdint.h>

/* Sanity */
#ifdef __USBCORE
#define _USBCORE_API __declspec(dllexport)
#else
#define _USBCORE_API __declspec(dllimport)
#endif

/* Definitions */
#define USB_MAX_PORTS			16
#define USB_MAX_VERSIONS		4
#define USB_MAX_INTERFACES		8
#define USB_MAX_ENDPOINTS		16

/* Usb Devices Class Codes */
#define USB_CLASS_INTERFACE		0x00	/* Get from Interface */
#define USB_CLASS_AUDIO			0x01
#define USB_CLASS_CDC			0x02
#define USB_CLASS_HID			0x03
#define USB_CLASS_PHYSICAL		0x05
#define USB_CLASS_IMAGE			0x06
#define USB_CLASS_PRINTER		0x07
#define USB_CLASS_MSD			0x08
#define USB_CLASS_HUB			0x09
#define USB_CLASS_CDC_DATA		0x0A
#define USB_CLASS_SMART_CARD	0x0B
#define USB_CLASS_SECURITY		0x0D
#define USB_CLASS_VIDEO			0x0E
#define USB_CLASS_HEALTHCARE	0x0F
#define USB_CLASS_DIAGNOSIS		0xDC
#define USB_CLASS_WIRELESS		0xE0
#define USB_CLASS_MISC			0xEF
#define USB_CLASS_APP_SPECIFIC	0xFE
#define USB_CLASS_VENDOR_SPEC	0xFF

/* Packet Request Targets */

/* Bit 7 */
#define USB_REQUEST_DIR_IN				0x80
#define USB_REQUEST_DIR_OUT				0x0

/* Bit 5-6 */
#define USB_REQUEST_TARGET_CLASS		0x20

/* Bits 0-4 */
#define USB_REQUEST_TARGET_DEVICE		0x0
#define USB_REQUEST_TARGET_INTERFACE	0x1
#define USB_REQUEST_TARGET_ENDPOINT		0x2
#define USB_REQUEST_TARGET_OTHER		0x3

/* Features */
#define USB_ENDPOINT_HALT			0x0

/* Packet Request Types */
#define USB_REQUEST_GET_STATUS		0x00
#define USB_REQUEST_CLR_FEATURE		0x01
#define USB_REQUEST_SET_FEATURE		0x03
#define USB_REQUEST_SET_ADDR		0x05
#define USB_REQUEST_GET_DESC		0x06
#define USB_REQUEST_SET_DESC		0x07
#define USB_REQUEST_GET_CONFIG		0x08
#define USB_REQUEST_SET_CONFIG		0x09
#define USB_REQUEST_GET_INTERFACE	0x0A
#define USB_REQUEST_SET_INTERFACE	0x0B
#define USB_REQUEST_SYNC_FRAME		0x0C
#define USB_REQUEST_RESET_IF		0xFF

/* Descriptor Types */
#define USB_DESC_TYPE_DEVICE		0x01
#define USB_DESC_TYPE_CONFIG		0x02
#define USB_DESC_TYPE_STRING		0x03
#define USB_DESC_TYPE_INTERFACE		0x04 //Interface
#define USB_DESC_TYPE_ENDPOINT		0x05
#define USB_DESC_TYPE_DEV_QAL		0x06 //DEVICE QUALIFIER
#define USB_DESC_TYPE_OSC			0x07 //Other Speed Config
#define USB_DESC_TYPE_INTERFACE_PWR	0x08	//Interface Power
#define USB_DESC_TYPE_OTG			0x09
#define USB_DESC_TYPE_DEBUG			0x0A
#define USB_DESC_TYPE_INTERFACE_ASC	0x0B
#define USB_DESC_TYPE_BOS			0x0F
#define USB_DESC_DEV_CAPS			0x10
#define USB_DESC_SSPEED_EP_CPN		0x30
#define USB_DESC_SSPEED_ISO_EP_CPN	0x31

/* Structures */
#pragma pack(push, 1)
typedef struct _UsbPacket
{
	/* Request Direction (Bit 7: 1 -> IN, 0 - OUT) 
	 *                   (Bit 0-4: 0 -> Device, 1 -> Interface, 2 -> Endpoint, 3 -> Other, 4... Reserved) */
	uint8_t Direction;

	/* Request Type (see above) */
	uint8_t Type;

	/* Request Value */
	uint8_t ValueLo;
	uint8_t ValueHi;

	/* Request Index */
	uint16_t Index;

	/* Length */
	uint16_t Length;

} UsbPacket_t;
#pragma pack(pop)

#pragma pack(push, 1)
typedef struct _UsbDeviceDescriptor
{
	/* Descriptor Length (Bytes) */
	uint8_t Length;

	/* Descriptor Type */
	uint8_t Type;

	/* USB Release Number in Binary-Coded Decimal (i.e, 2.10 is expressed as 210h) */
	uint16_t UsbRnBcd;

	/* USB Class Code (USB-IF) */
	uint8_t Class;

	/* Usb Subclass Code (USB-IF) */
	uint8_t Subclass;

	/* Device Protocol Code (USB-IF) */
	uint8_t Protocol;

	/* Max packet size for endpoing zero (8, 16, 32, or 64 are the only valid options) */
	uint8_t MaxPacketSize;

	/* Vendor Id */
	uint16_t VendorId;

	/* Product Id */
	uint16_t ProductId;

	/* Device Release Number in Binary-Coded Decimal (i.e, 2.10 is expressed as 210h) */
	uint16_t DeviceRnBcd;

	/* String Descriptor Indexes */
	uint8_t StrIndexManufactor;
	uint8_t StrIndexProduct;
	uint8_t StrIndexSerialNum;

	/* Number of Configuration */
	uint8_t NumConfigurations;

} UsbDeviceDescriptor_t;
#pragma pack(pop)

#pragma pack(push, 1)
typedef struct _UsbConfigDescriptor
{
	/* Descriptor Length (Bytes) */
	uint8_t Length;

	/* Descriptor Type */
	uint8_t Type;

	/* The total combined length in bytes of all
	 * the descriptors returned with the request
	 * for this CONFIGURATION descriptor */
	uint16_t TotalLength;

	/* Number of Interfaces */
	uint8_t NumInterfaces;

	/* Configuration */
	uint8_t ConfigurationValue;

	/* String Index */
	uint8_t	StrIndexConfiguration;

	/* Attributes 
	 * Bit 6: 0 - Selfpowered, 1 - Local Power Source 
	 * Bit 7: 1 - Remote Wakeup Support */
	uint8_t Attributes;

	/* Power Consumption 
	 * Expressed in units of 2mA (i.e., a value of 50 in this field indicates 100mA) */
	uint8_t MaxPowerConsumption;

} UsbConfigDescriptor_t;
#pragma pack(pop)

#pragma pack(push, 1)
typedef struct _UsbInterfaceDescriptor
{
	/* Descriptor Length (Bytes) */
	uint8_t Length;

	/* Descriptor Type */
	uint8_t Type;

	/* Number of Interface */
	uint8_t NumInterface;

	/* Alternative Setting */
	uint8_t AlternativeSetting;

	/* Number of Endpoints other than endpoint zero */
	uint8_t NumEndpoints;
	
	/* USB Class Code (USB-IF) */
	uint8_t Class;

	/* Usb Subclass Code (USB-IF) */
	uint8_t Subclass;

	/* Device Protocol Code (USB-IF) */
	uint8_t Protocol;

	/* String Index */
	uint8_t StrIndexInterface;

} UsbInterfaceDescriptor_t;
#pragma pack(pop)

#pragma pack(push, 1)
typedef struct _UsbEndpointDescriptor
{
	/* Descriptor Length (Bytes) */
	uint8_t Length;

	/* Descriptor Type */
	uint8_t Type;

	/* Address 
	 * Bits 0-3: Endpoint Number
	 * Bit 7: 1 - In, 0 - Out */
	uint8_t Address;

	/* Attributes 
	 * Bits 0-1: Xfer Type (00 control, 01 isosync, 10 bulk, 11 interrupt) 
	 * Bits 2-3: Sync Type (00 no sync, 01 async, 10 adaptive, 11 sync) 
	 * Bits 4-5: Usage Type (00 data, 01 feedback) */
	uint8_t Attributes;

	/* Maximum Packet Size (Bits 0-10) (Bits 11-15: 0) */
	uint16_t MaxPacketSize;

	/* Interval */
	uint8_t Interval;

	/* OPTIONAL - bRefresh */
	uint8_t Refresh;

	/* OPTIONAL - bSyncAddress */
	uint8_t SyncAddress;

} UsbEndpointDescriptor_t;
#pragma pack(pop)

#pragma pack(push, 1)
typedef struct _UsbStringDescriptor
{
	/* Descriptor Length (Bytes) */
	uint8_t Length;

	/* Descriptor Type (3) */
	uint8_t Type;

	/* Data */
	uint16_t WString[10];

} UsbStringDescriptor_t;
#pragma pack(pop)

/* Types of String */
#define USB_LANGUAGE_ARABIC		0x401
#define USB_LANGUAGE_CHINESE	0x404
#define USB_LANGUAGE_GERMAN		0x407
#define USB_LANGUAGE_ENGLISH	0x409
#define USB_LANGUAGE_SPANISH	0x40A
#define USB_LANGUAGE_FRENCH		0x40C
#define USB_LANGUAGE_ITALIAN	0x410
#define USB_LANGUAGE_JAPANESE	0x411
#define USB_LANGUAGE_PORTUGUESE	0x416
#define USB_LANGUAGE_RUSSIAN	0x419

#pragma pack(push, 1)
typedef struct _UsbUnicodeStringDescriptor
{
	/* Descriptor Length (Bytes) 2 + 2 * numUnicodeCharacters */
	uint8_t Length;

	/* Descriptor Type (3) */
	uint8_t Type;

	/* Data */
	uint8_t String[60];

} UsbUnicodeStringDescriptor_t;
#pragma pack(pop)

/* Enumerations */
typedef enum _UsbHcEndpointType
{
	EndpointControl			= 0,
	EndpointIsochronous		= 1,
	EndpointBulk			= 2,
	EndpointInterrupt		= 3

} UsbHcEndpointType_t;

/* The Abstract Usb Endpoint */
typedef struct _UsbHcEndpoint
{
	/* Type */
	UsbHcEndpointType_t Type;

	/* Address */
	size_t Address;

	/* Direction (IN, OUT) */
	size_t Direction;

	/* Max Packet Size */
	size_t MaxPacketSize;

	/* Bandwidth */
	size_t Bandwidth;

	/* Data Toggle */
	size_t Toggle;

	/* Poll Interval */
	size_t Interval;

	/* Endpoint Data */
	void *AttachedData;

} UsbHcEndpoint_t;

#define USB_EP_DIRECTION_IN		0x0
#define USB_EP_DIRECTION_OUT	0x1
#define USB_EP_DIRECTION_BOTH	0x2

/* The Abstract Usb Interface Version */
typedef struct _UsbHcInterfaceVersion
{
	/* Id */
	int VersionId;

	/* Ep Numbers */
	size_t NumEndpoints;

	/* Ep's */
	UsbHcEndpoint_t **Endpoints;

} UsbHcInterfaceVersion_t;

/* The Abstract Usb Interface */
typedef struct _UsbHcInterface
{
	/* Interface Type */
	size_t Id;
	size_t Class;
	size_t Subclass;
	size_t Protocol;

	/* Versions */
	UsbHcInterfaceVersion_t *Versions[USB_MAX_VERSIONS];

	/* Driver Data */
	void(*Destroy)(void*, int);
	void *DriverData;

} UsbHcInterface_t;

/* The Abstract Device */
#pragma pack(push, 1)
typedef struct _UsbHcDevice
{
	/* Device Information */
	uint8_t Class;
	uint8_t Subclass;
	uint8_t Protocol;

	/* Device Id's */
	uint16_t VendorId;
	uint16_t ProductId;

	/* Configuration */
	uint8_t NumConfigurations;
	uint16_t ConfigMaxLength;
	uint16_t MaxPowerConsumption;
	uint16_t MaxPacketSize;
	uint8_t Configuration;

	/* String Ids */
	uint8_t StrIndexProduct;
	uint8_t StrIndexManufactor;
	uint8_t StrIndexSerialNum;

	/* Host Driver (UsbHc_t) & Port Number */
	void *HcDriver;
	size_t Port;

	/* Device Address */
	size_t Address;

	/* Device Descriptors (Config,Interface,Endpoint,Hid,etc) */
	void *Descriptors;
	size_t DescriptorsLength;

	/* Device Interfaces */
	size_t NumInterfaces;
	UsbHcInterface_t *Interfaces[USB_MAX_INTERFACES];

	/* Control Endpoint */
	UsbHcEndpoint_t *CtrlEndpoint;

} UsbHcDevice_t;
#pragma pack(pop)

/* Transaction Type */
typedef enum _UsbTransactionType
{
	SetupTransaction,
	InTransaction,
	OutTransaction

} UsbTransactionType_t;

/* The Abstract Transaction 
 * A request consists of several transactions */
typedef struct _UsbHcTransaction
{
	/* Type */
	UsbTransactionType_t Type;

	/* A Transfer Descriptor Ptr */
	void *TransferDescriptor;
	void *TransferDescriptorCopy;

	/* Transfer Descriptor Buffer */
	void *TransferBuffer;

	/* Target/Source Buffer */
	void *IoBuffer;

	/* Target/Source Buffer Length */
	size_t IoLength;

	/* Next Transaction */
	struct _UsbHcTransaction *Link;

} UsbHcTransaction_t;

/* The Abstract Usb Callback */
typedef struct _UsbInterruptCallback
{
	/* Callback */
	void(*Callback)(void*);

	/* Callback arguments */
	void *Args;

} UsbInterruptCallback_t;

/* Transfer Types */
typedef enum _UsbTransferType
{
	ControlTransfer,
	BulkTransfer,
	InterruptTransfer,
	IsochronousTransfer

} UsbTransferType_t;

/* Transfer Status */
typedef enum _UsbTransferStatus
{
	TransferNotProcessed,
	TransferFinished,
	TransferStalled,
	TransferNotResponding,
	TransferInvalidToggles,
	TransferInvalidData,
	TransferNAK

} UsbTransferStatus_t;

/* Device Speed */
typedef enum _UsbSpeed
{
	LowSpeed,		/* 1.0 / 1.1 */
	FullSpeed,		/* 1.0 / 1.1 */
	HighSpeed,		/* 2.0 */
	SuperSpeed		/* 3.0 */

} UsbSpeed_t;

/* The Abstract Transfer Request */
typedef struct _UsbHcRequest
{
	/* Bulk or Control? */
	UsbTransferType_t Type;
	UsbSpeed_t Speed;

	/* Transfer Specific Information */
	void *Data;
	UsbHcDevice_t *Device;

	/* Endpoint */
	UsbHcEndpoint_t *Endpoint;

	/* Transaction Information */
	size_t TokenBytes;

	/* Buffer Information */
	void *IoBuffer;
	size_t IoLength;

	/* Packet */
	UsbPacket_t Packet;

	/* Any callback associated? */
	UsbInterruptCallback_t *Callback;

	/* The Transaction List */
	UsbHcTransaction_t *Transactions;

	/* Is it done? */
	UsbTransferStatus_t Status;

} UsbHcRequest_t;

/* The Abstract Port */
typedef struct _UsbHcPort
{
	/* Port Number */
	size_t Id;

	/* Connection Status */
	int Connected;

	/* Enabled Status */
	int Enabled;

	/* Speed */
	UsbSpeed_t Speed;

	/* Device Connected */
	UsbHcDevice_t *Device;

} UsbHcPort_t;

/* Controller Type */
typedef enum _UsbControllerType
{
	OhciController,
	UhciController,
	EhciController,
	XhciController

} UsbControllerType_t;

/* The Abstract Controller */
typedef struct _UsbHc
{
	/* Controller Type */
	UsbControllerType_t Type;

	/* Controller Data */
	void *Hc;

	/* Controller Info */
	size_t NumPorts;

	/* Address Map 
	 * 4 x 32 bits = 128 possible addresses
	 * which match the max in usb-spec */
	uint32_t AddressMap[4];

	/* Ports */
	UsbHcPort_t *Ports[USB_MAX_PORTS];

	/* Port Functions */
	void(*PortSetup)(void*, UsbHcPort_t*);
	
	/* Endpoint Functions */
	void (*EndpointSetup)(void*, UsbHcEndpoint_t*);
	void (*EndpointDestroy)(void*, UsbHcEndpoint_t*);

	/* Callback Functions */
	void (*RootHubCheck)(void*);
	void (*Reset)(void*);

	/* Transaction Functions */
	void (*TransactionInit)(void*, UsbHcRequest_t*);
	UsbHcTransaction_t *(*TransactionSetup)(void*, UsbHcRequest_t*);
	UsbHcTransaction_t *(*TransactionIn)(void*, UsbHcRequest_t*);
	UsbHcTransaction_t *(*TransactionOut)(void*, UsbHcRequest_t*);
	void (*TransactionSend)(void*, UsbHcRequest_t*);
	void (*TransactionDestroy)(void*, UsbHcRequest_t*);

} UsbHc_t;

/* Usb Event Types */
typedef enum _UsbEventType
{
	HcdConnectedEvent,
	HcdDisconnectedEvent,
	HcdTransferEvent,
	HcdRootHubEvent,
	HcdFatalEvent

} UsbEventType_t;

/* Usb Event */
typedef struct _UsbEvent
{
	/* Event Type */
	UsbEventType_t Type;

	/* Controller */
	UsbHc_t *Controller;

	/* Port */
	int Port;

} UsbEvent_t;

/* Prototypes */

/* Returns an controller ID for used with identification */
_USBCORE_API UsbHc_t *UsbInitController(void *Data, UsbControllerType_t Type, size_t Ports);
_USBCORE_API int UsbRegisterController(UsbHc_t *Controller);

/* Transfer Utilities */
_USBCORE_API void UsbTransactionInit(UsbHc_t *Hc, UsbHcRequest_t *Request, UsbTransferType_t Type,
								    UsbHcDevice_t *Device, UsbHcEndpoint_t *Endpoint);
_USBCORE_API void UsbTransactionSetup(UsbHc_t *Hc, UsbHcRequest_t *Request, size_t PacketSize);
_USBCORE_API void UsbTransactionIn(UsbHc_t *Hc, UsbHcRequest_t *Request, int Handshake, void *Buffer, size_t Length);
_USBCORE_API void UsbTransactionOut(UsbHc_t *Hc, UsbHcRequest_t *Request, int Handshake, void *Buffer, size_t Length);
_USBCORE_API void UsbTransactionSend(UsbHc_t *Hc, UsbHcRequest_t *Request);
_USBCORE_API void UsbTransactionDestroy(UsbHc_t *Hc, UsbHcRequest_t *Request);

/* Functions */
_USBCORE_API UsbTransferStatus_t UsbFunctionSetAddress(UsbHc_t *Hc, int Port, size_t Address);
_USBCORE_API UsbTransferStatus_t UsbFunctionGetDeviceDescriptor(UsbHc_t *Hc, int Port);
_USBCORE_API UsbTransferStatus_t UsbFunctionGetConfigDescriptor(UsbHc_t *Hc, int Port);
_USBCORE_API UsbTransferStatus_t UsbFunctionSetConfiguration(UsbHc_t *Hc, int Port, size_t Configuration);
_USBCORE_API UsbTransferStatus_t UsbFunctionGetStringDescriptor(UsbHc_t *Hc, int Port);
_USBCORE_API UsbTransferStatus_t UsbFunctionGetDescriptor(UsbHc_t *Hc, int Port, void *Buffer, uint8_t Direction,
											uint8_t DescriptorType, uint8_t SubType, 
											uint8_t DescriptorIndex, uint16_t DescriptorLength);
_USBCORE_API UsbTransferStatus_t UsbFunctionClearFeature(UsbHc_t *Hc, int Port,
										uint8_t Target, uint16_t Index, uint16_t Feature);
_USBCORE_API UsbTransferStatus_t UsbFunctionSetFeature(UsbHc_t *Hc, int Port,
									  uint8_t Target, uint16_t Index, uint16_t Feature);

/* Send packet */
_USBCORE_API UsbTransferStatus_t UsbFunctionSendPacket(UsbHc_t *Hc, int Port, void *Buffer, uint8_t RequestType,
											uint8_t Request, uint8_t ValueHi, uint8_t ValueLo, 
											uint16_t Index, uint16_t Length);

/* Interrupt Transfers */
/* Install Interrupt Pipe */
_USBCORE_API void UsbFunctionInstallPipe(UsbHc_t *Hc, UsbHcDevice_t *Device, UsbHcRequest_t *Request,
											UsbHcEndpoint_t *Endpoint, void *Buffer, size_t Length);

/* Events */
_USBCORE_API void UsbEventCreate(UsbHc_t *Hc, int Port, UsbEventType_t Type);

/* Gets */
_USBCORE_API UsbHc_t *UsbGetHcd(int ControllerId);
_USBCORE_API UsbHcPort_t *UsbGetPort(UsbHc_t *Controller, int Port);

#endif // !USB_H_

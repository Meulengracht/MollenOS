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
* MollenOS USB Core Driver
*/

#ifndef __MODULE_USBCORE__
#define __MODULE_USBCORE__

/* Includes */
#include <Arch.h>

/* C-Library */
#include <crtdefs.h>
#include <stdint.h>
#include <ds/mstring.h>

/* Definitions */
#define USB_WATCHDOG_INTERVAL	1000

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
	/* Interface Name */
	MString_t *Name;

	/* Interface Type */
	size_t Id;
	size_t Class;
	size_t Subclass;
	size_t Protocol;

	/* Strings */
	size_t StrIndex;

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
	MString_t *Name;
	MString_t *Manufactor;
	MString_t *SerialNumber;

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
	int NumLanguages;
	uint16_t *Languages;
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
	/* Transaction Type */
	UsbTransactionType_t Type;

	/* Transaction Descriptor,
	 * this is controller specific 
	 * for instance, OHCI/UHCI 
	 * this might be a TransferDescriptor
	 * while it can be other stuff in EHCI */
	void *TransferDescriptor;
	void *TransferDescriptorCopy;

	/* This is the temporary/proxy buffer
	 * allocated for the transaction, this
	 * insures we get page aligned transfers */
	void *TransferBuffer;

	/* The actual Target/Source Buffer 
	 * for this transaction */
	void *Buffer;

	/* The buffer length
	 * requested for this transaction */
	size_t Length;

	/* The actual bytes transferred 
	 * during this transaction */
	size_t ActualLength;

	/* Next Transaction */
	struct _UsbHcTransaction *Link;

} UsbHcTransaction_t;

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
	TransferNAK,
	TransferBabble

} UsbTransferStatus_t;

/* Device Speed */
typedef enum _UsbSpeed
{
	LowSpeed,		/* 1.0 / 1.1 */
	FullSpeed,		/* 1.0 / 1.1 */
	HighSpeed,		/* 2.0 */
	SuperSpeed		/* 3.0 */

} UsbSpeed_t;

/* The Abstract Usb Callback */
typedef struct _UsbInterruptCallback
{
	/* Callback */
	void(*Callback)(void*, UsbTransferStatus_t);

	/* Callback arguments */
	void *Args;

} UsbInterruptCallback_t;

/* The Abstract Transfer Request */
typedef struct _UsbHcRequest
{
	/* Transfer Type 
	 * Ctrl, Bulk, Int, Isoc */
	UsbTransferType_t Type;

	/* Transfer Speed 
	 * Low, Full, High, Super */
	UsbSpeed_t Speed;

	/* Transfer Device/Endpoint 
	 * For easy access */
	UsbHcDevice_t *Device;
	UsbHcEndpoint_t *Endpoint;

	/* Transfer specific information
	 * this is usually a queue-head 
	 * pointer - Controller Specific */
	void *Data;

	/* Any callback associated? */
	UsbInterruptCallback_t *Callback;

	/* Transaction list + count */
	UsbHcTransaction_t *Transactions;
	int TransactionCount;

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

	/* Transaction Split Behaviors */
	size_t ControlOverride;
	size_t BulkOverride;
	size_t InterruptOverride;
	size_t IsocOverride;

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
	void (*Watchdog)(void*);

	/* Transaction Functions */
	void (*TransactionInit)(void*, UsbHcRequest_t*);
	UsbHcTransaction_t *(*TransactionSetup)(void*, UsbHcRequest_t*, UsbPacket_t *Packet);
	UsbHcTransaction_t *(*TransactionIn)(void*, UsbHcRequest_t*, void *Buffer, size_t Length);
	UsbHcTransaction_t *(*TransactionOut)(void*, UsbHcRequest_t*, void *Buffer, size_t Length);
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
_USBCORE_API void UsbTransactionSetup(UsbHc_t *Hc, UsbHcRequest_t *Request, UsbPacket_t *Packet);
_USBCORE_API void UsbTransactionIn(UsbHc_t *Hc, UsbHcRequest_t *Request, int Handshake, void *Buffer, size_t Length);
_USBCORE_API void UsbTransactionOut(UsbHc_t *Hc, UsbHcRequest_t *Request, int Handshake, void *Buffer, size_t Length);
_USBCORE_API void UsbTransactionSend(UsbHc_t *Hc, UsbHcRequest_t *Request);
_USBCORE_API void UsbTransactionDestroy(UsbHc_t *Hc, UsbHcRequest_t *Request);

/* Functions */
_USBCORE_API UsbTransferStatus_t UsbFunctionSetAddress(UsbHc_t *Hc, int Port, size_t Address);
_USBCORE_API UsbTransferStatus_t UsbFunctionGetDeviceDescriptor(UsbHc_t *Hc, int Port);
_USBCORE_API UsbTransferStatus_t UsbFunctionGetConfigDescriptor(UsbHc_t *Hc, int Port);
_USBCORE_API UsbTransferStatus_t UsbFunctionSetConfiguration(UsbHc_t *Hc, int Port, size_t Configuration);
_USBCORE_API UsbTransferStatus_t UsbFunctionGetStringLanguages(UsbHc_t *Hc, int Port);
_USBCORE_API UsbTransferStatus_t UsbFunctionGetStringDescriptor(UsbHc_t *Hc, int Port, size_t LanguageId, size_t StringIndex, char *StrBuffer);
_USBCORE_API UsbTransferStatus_t UsbFunctionGetDescriptor(UsbHc_t *Hc, int Port, void *Buffer, uint8_t Direction,
											uint8_t DescriptorType, uint8_t SubType, 
											uint8_t DescriptorIndex, uint16_t DescriptorLength);
_USBCORE_API UsbTransferStatus_t UsbFunctionClearFeature(UsbHc_t *Hc, int Port,
										uint8_t Target, uint16_t Index, uint16_t Feature);
_USBCORE_API UsbTransferStatus_t UsbFunctionSetFeature(UsbHc_t *Hc, int Port,
									  uint8_t Target, uint16_t Index, uint16_t Feature);

/* Send packet */
_USBCORE_API UsbTransferStatus_t UsbFunctionSendPacket(UsbHc_t *Hc, int Port, void *Buffer, uint8_t RequestType,
											uint8_t pRequest, uint8_t ValueHi, uint8_t ValueLo, 
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

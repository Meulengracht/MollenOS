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
* MollenOS X86 USB Core MSD Driver
*/

/* Includes */
#include <Module.h>
#include <UsbMsd.h>
#include <Semaphore.h>
#include <Heap.h>
#include <List.h>
#include <Timers.h>
#include <Log.h>

#include <string.h>

/* Sense Codes */
const char* SenseKeys[] =
{
	"No Sense",
	"Recovered Error - last command completed with some recovery action",
	"Not Ready - logical unit addressed cannot be accessed",
	"Medium Error - command terminated with a non-recovered error condition",
	"Hardware Error",
	"Illegal Request - illegal parameter in the command descriptor block",
	"Unit Attention - disc drive may have been reset.",
	"Data Protect - command read/write on a protected block",
	"Undefined",
	"Firmware Error",
	"Undefined",
	"Aborted Command - disc drive aborted the command",
	"Equal - SEARCH DATA command has satisfied an equal comparison",
	"Volume Overflow - buffered peripheral device has reached the end of medium partition",
	"Miscompare - source data did not match the data read from the medium",
	"Undefined"
};

uint32_t rev32(uint32_t dword)
{
	return ((dword >> 24) & 0x000000FF) 
		| ((dword >> 8) & 0x0000FF00) 
		| ((dword << 8) & 0x00FF0000) 
		| ((dword << 24) & 0xFF000000);
}

uint64_t rev64(uint64_t qword)
{
	uint64_t y;
	char* px = (char*)&qword;
	char* py = (char*)&y;
	for (int i = 0; i<sizeof(uint64_t); i++)
		py[i] = px[sizeof(uint64_t) - 1 - i];
	return y;
}

/* Prototypes */
void UsbMsdDestroy(void *UsbDevice);

/* Msd Specific Requests */
void UsbMsdReset(MsdDevice_t *Device);
void UsbMsdGetMaxLUN(MsdDevice_t *Device);
void UsbMsdReadyDevice(MsdDevice_t *Device);
void UsbMsdReadCapacity(MsdDevice_t *Device, MCoreStorageDevice_t *sDevice);

/* In / Out */
UsbTransferStatus_t UsbMsdSendSCSICommandIn(uint8_t ScsiCommand, MsdDevice_t *Device,
	uint64_t SectorLBA, void *Buffer, uint32_t DataLength);

/* Read & Write for MSD's */
int UsbMsdReadSectors(void *DevData, uint64_t SectorLBA, void *Buffer, uint32_t BufferLength);
int UsbMsdWriteSectors(void *DevData, uint64_t SectorLBA, void *Buffer, uint32_t BufferLength);

/* Initialise Driver for a MSD */
void UsbMsdInit(UsbHcDevice_t *UsbDevice, uint32_t InterfaceIndex)
{
	/* Allocate */
	MsdDevice_t *DevData = (MsdDevice_t*)kmalloc(sizeof(MsdDevice_t));
	MCoreStorageDevice_t *StorageData = (MCoreStorageDevice_t*)kmalloc(sizeof(MCoreStorageDevice_t));
	UsbHc_t *UsbHcd = (UsbHc_t*)UsbDevice->HcDriver;
	uint32_t i;

	/* Sanity */
	if (UsbDevice->Interfaces[InterfaceIndex]->Subclass == X86_USB_MSD_SUBCLASS_FLOPPY)
		DevData->IsUFI = 1;
	else
		DevData->IsUFI = 0;

	/* Set */
	StorageData->DiskData = DevData;
	StorageData->SectorSize = 512;
	StorageData->SectorCount = 0;

	/* Depends if floppy */
	if (!DevData->IsUFI)
	{
		StorageData->AlignedAccess = 0;
		StorageData->SectorsPerCylinder = 64;
	}
	else
	{
		StorageData->AlignedAccess = 1;
		StorageData->SectorsPerCylinder = 18;
	}

	/* Set functions */
	StorageData->Read = UsbMsdReadSectors;
	StorageData->Write = UsbMsdWriteSectors;

	DevData->UsbDevice = UsbDevice;
	DevData->Interface = InterfaceIndex;
	DevData->EpIn = NULL;
	DevData->EpOut = NULL;
	DevData->EpInterrupt = NULL;
	DevData->LUNCount = 0;
	DevData->IsReady = 0;
	DevData->SectorSize = 512;

	/* Locate neccessary endpoints */
	for (i = 0; i < UsbDevice->Interfaces[InterfaceIndex]->NumEndpoints; i++)
	{
		/* Interrupt? */
		if (UsbDevice->Interfaces[InterfaceIndex]->Endpoints[i]->Type == X86_USB_EP_TYPE_INTERRUPT)
			DevData->EpInterrupt = UsbDevice->Interfaces[InterfaceIndex]->Endpoints[i];

		/* Bulk? */
		if (UsbDevice->Interfaces[InterfaceIndex]->Endpoints[i]->Type == X86_USB_EP_TYPE_BULK)
		{
			/* In or out? */
			if (UsbDevice->Interfaces[InterfaceIndex]->Endpoints[i]->Direction == X86_USB_EP_DIRECTION_IN)
				DevData->EpIn = UsbDevice->Interfaces[InterfaceIndex]->Endpoints[i];
			else if (UsbDevice->Interfaces[InterfaceIndex]->Endpoints[i]->Direction == X86_USB_EP_DIRECTION_OUT)
				DevData->EpOut = UsbDevice->Interfaces[InterfaceIndex]->Endpoints[i];
		}
	}

	/* Sanity Ep's */
	if (DevData->EpIn == NULL
		|| DevData->EpOut == NULL)
	{
		LogDebug("USBM", "Msd is missing either in or out endpoint");
		kfree(DevData);
		return;
	}

	/* Reset Toggles */
	DevData->EpIn->Toggle = 0;
	DevData->EpOut->Toggle = 0;

	/* Save Data */
	UsbDevice->Destroy = UsbMsdDestroy;
	UsbDevice->DriverData = (void*)StorageData;

	/* Setup endpoints */
	UsbHcd->EndpointSetup(UsbHcd->Hc, DevData->EpIn);
	UsbHcd->EndpointSetup(UsbHcd->Hc, DevData->EpOut);

	/* Test & Setup Disk */

	/* Reset Bulk */
	if (DevData->IsUFI == 0)
		UsbMsdReset(DevData);

	/* Send Inquiry */
	ScsiInquiry_t InquiryData;
	if (UsbMsdSendSCSICommandIn(X86_SCSI_INQUIRY, DevData, 0, &InquiryData, sizeof(ScsiInquiry_t))
		!= TransferFinished)
		LogDebug("USBM", "Failed to execute Inquiry Command");

	/* Send Test-Unit-Ready */
	i = 3;
	if (DevData->IsUFI)
		i = 30;

	while (DevData->IsReady == 0
		&& i != 0)
	{
		/* Test */
		UsbMsdReadyDevice(DevData);

		/* Did it work? */
		if (DevData->IsReady == 1)
			break; 

		/* Fuck, try again soon */
		StallMs(50);
		i--;
	}

	/* Did we fail to ready device? */
	if (!DevData->IsReady)
	{
		LogDebug("USBM", "Failed to ready MSD device");
		return;
	}

	/* Read Capabilities 10
	 * If it returns 0xFFFFFFFF 
	 * Use Read Capabilities 16 */
	UsbMsdReadCapacity(DevData, StorageData);

	/* Debug */
	LogInformation("USBM", "MSD SectorCount: 0x%x, SectorSize: 0x%x",
		(uint32_t)StorageData->SectorCount, StorageData->SectorSize);

	/* Register Us */
	DevData->DeviceId =
		DmCreateDevice("Usb Storage", DeviceStorage, (void*)StorageData);
}

/* Cleanup */
void UsbMsdDestroy(void *UsbDevice)
{
	/* Cast */
	UsbHcDevice_t *Dev = (UsbHcDevice_t*)UsbDevice;
	MCoreStorageDevice_t *StorageData = (MCoreStorageDevice_t*)Dev->DriverData;
	MsdDevice_t *Device = (MsdDevice_t*)StorageData->DiskData;
	UsbHc_t *UsbHcd = (UsbHc_t*)Dev->HcDriver;

	/* Unregister Us */
	DmDestroyDevice(Device->DeviceId);

	/* Free endpoints */
	UsbHcd->EndpointDestroy(UsbHcd->Hc, Device->EpIn);
	UsbHcd->EndpointDestroy(UsbHcd->Hc, Device->EpOut);
	
	/* Free Data */
	kfree(StorageData);
	kfree(Device);
}

/* Msd Specific Requests */
void UsbMsdReset(MsdDevice_t *Device)
{
	/* Reset is 
	* 0x21 | 0xFF | wIndex - Interface */

	/* Send control packet - Interface Reset */
	UsbFunctionSendPacket((UsbHc_t*)Device->UsbDevice->HcDriver, Device->UsbDevice->Port,
		NULL, X86_USB_REQ_TARGET_CLASS | X86_USB_REQ_TARGET_INTERFACE,
		X86_USB_MSD_REQUEST_RESET, 0, 0, (uint16_t)Device->Interface, 0);

	/* The value of the data toggle bits shall be preserved 
	 * it says in the usbmassbulk spec */
}

void UsbMsdResetRecovery(MsdDevice_t *Device)
{
	/* Step 1 - Do Reset */
	UsbMsdReset(Device);

	/* Step 2 - Clear HALT features */
	UsbFunctionClearFeature((UsbHc_t*)Device->UsbDevice->HcDriver,
		Device->UsbDevice->Port, X86_USB_REQ_TARGET_ENDPOINT,
		(uint16_t)Device->EpIn->Address, X86_USB_ENDPOINT_HALT);
	UsbFunctionClearFeature((UsbHc_t*)Device->UsbDevice->HcDriver,
		Device->UsbDevice->Port, X86_USB_REQ_TARGET_ENDPOINT,
		(uint16_t)Device->EpOut->Address, X86_USB_ENDPOINT_HALT);

	/* Set Configuration back to initial */
	UsbFunctionSetConfiguration((UsbHc_t*)Device->UsbDevice->HcDriver,
		Device->UsbDevice->Port, 1);

	/* Reset Toggles */
	Device->EpIn->Toggle = 0;
	Device->EpOut->Toggle = 0;

	/* Reset Interface */
	UsbMsdReset(Device);
}

void UsbMsdGetMaxLUN(MsdDevice_t *Device)
{
	/* Get Max LUNS is
	* 0xA1 | 0xFE | wIndex - Interface 
	* 1 Byte is expected back, values range between 0 - 15, 0-indexed */
	UsbTransferStatus_t Status;
	uint8_t MaxLuns = 0;

	/* Send control packet - Interface Reset */
	Status = UsbFunctionSendPacket((UsbHc_t*)Device->UsbDevice->HcDriver, Device->UsbDevice->Port,
		&MaxLuns, X86_USB_REQ_DIRECTION_IN | X86_USB_REQ_TARGET_CLASS | X86_USB_REQ_TARGET_INTERFACE,
		X86_USB_MSD_REQUEST_RESET, 0, 0, (uint16_t)Device->Interface, 1);

	/* If no multiple LUNS are supported, device may STALL
	* it says in the usbmassbulk spec */
	if (Status == TransferFinished)
		Device->LUNCount = (uint32_t)(MaxLuns & 0xF);
	else if (Status == TransferStalled)
		UsbMsdResetRecovery(Device);
}

/* Scsi Helpers */
void UsbMsdBuildSCSICommand(uint8_t ScsiCommand,
	MsdCommandBlockWrap_t *CmdBlock, uint64_t SectorLBA, uint32_t DataLen, uint16_t SectorSize)
{
	/* Memset */
	memset((void*)CmdBlock, 0, sizeof(MsdCommandBlockWrap_t));

	/* Set shared */
	CmdBlock->Signature = X86_USB_MSD_CBW_SIGNATURE;
	CmdBlock->Tag = X86_USB_MSD_TAG_SIGNATURE | ScsiCommand;

	CmdBlock->DataLen = DataLen;
	CmdBlock->CommandBytes[0] = ScsiCommand;

	/* Now it depends */
	switch (ScsiCommand)
	{
		/* Test Unit Ready - 6 */
	case X86_SCSI_TEST_UNIT_READY:
	{
		/* Request goes OUT */
		CmdBlock->Flags = X86_USB_MSD_CBW_OUT;
		CmdBlock->Length = 6;

	} break;

	/* Request Sense - 6 */
	case X86_SCSI_REQUEST_SENSE:
	{
		/* Request goes IN */
		CmdBlock->Flags = X86_USB_MSD_CBW_IN;
		CmdBlock->Length = 6;

		/* Length of allocation */
		CmdBlock->CommandBytes[4] = 18;

	} break;

	/* Inquiry - 6 */
	case X86_SCSI_INQUIRY:
	{
		/* Request goes IN */
		CmdBlock->Flags = X86_USB_MSD_CBW_IN;
		CmdBlock->Length = 6;

		/* Length of allocation */
		CmdBlock->CommandBytes[4] = 36;

	} break;

	/* Read Capacities - 10 */
	case X86_SCSI_READ_CAPACITY:
	{
		/* Request goes IN */
		CmdBlock->Flags = X86_USB_MSD_CBW_IN;
		CmdBlock->Length = 10;

		/* Sector Lba */
		CmdBlock->CommandBytes[2] = ((SectorLBA >> 24) & 0xFF);
		CmdBlock->CommandBytes[3] = ((SectorLBA >> 16) & 0xFF);
		CmdBlock->CommandBytes[4] = ((SectorLBA >> 8) & 0xFF);
		CmdBlock->CommandBytes[5] = (SectorLBA & 0xFF);

	} break;

	/* Read Capacities - 16 */
	case X86_SCSI_READ_CAPACITY_16:
	{
		/* Request goes IN */
		CmdBlock->Flags = X86_USB_MSD_CBW_IN;
		CmdBlock->Length = 16;

		/* Service Action */
		CmdBlock->CommandBytes[1] = 0x10;

		/* Sector Lba */	
		CmdBlock->CommandBytes[2] = ((SectorLBA >> 56) & 0xFF);
		CmdBlock->CommandBytes[3] = ((SectorLBA >> 48) & 0xFF);
		CmdBlock->CommandBytes[4] = ((SectorLBA >> 40) & 0xFF);
		CmdBlock->CommandBytes[5] = ((SectorLBA >> 32) & 0xFF);
		CmdBlock->CommandBytes[6] = ((SectorLBA >> 24) & 0xFF);
		CmdBlock->CommandBytes[7] = ((SectorLBA >> 16) & 0xFF);
		CmdBlock->CommandBytes[8] = ((SectorLBA >> 8) & 0xFF);
		CmdBlock->CommandBytes[9] = (SectorLBA & 0xFF);

		/* Sector Count */
		CmdBlock->CommandBytes[10] = ((DataLen << 24) & 0xFF);
		CmdBlock->CommandBytes[11] = ((DataLen << 16) & 0xFF);
		CmdBlock->CommandBytes[12] = ((DataLen << 8) & 0xFF);
		CmdBlock->CommandBytes[13] = (DataLen & 0xFF);
	}

	/* Read - 10 */
	case X86_SCSI_READ:
	{
		/* Calculate block count */
		uint16_t NumSectors = (uint16_t)(DataLen / SectorSize);
		if (DataLen % SectorSize)
			NumSectors++;

		/* Request goes IN */
		CmdBlock->Flags = X86_USB_MSD_CBW_IN;
		CmdBlock->Length = 10;

		/* Sector Lba */
		CmdBlock->CommandBytes[2] = ((SectorLBA >> 24) & 0xFF);
		CmdBlock->CommandBytes[3] = ((SectorLBA >> 16) & 0xFF);
		CmdBlock->CommandBytes[4] = ((SectorLBA >> 8) & 0xFF);
		CmdBlock->CommandBytes[5] = (SectorLBA & 0xFF);

		/* Sector Count */
		CmdBlock->CommandBytes[7] = ((NumSectors << 8) & 0xFF);
		CmdBlock->CommandBytes[8] = (NumSectors & 0xFF);

	} break;

	/* Write - 10 */
	case X86_SCSI_WRITE:
	{
		/* Calculate block count */
		uint16_t NumSectors = (uint16_t)(DataLen / SectorSize);
		if (DataLen % SectorSize)
			NumSectors++;

		/* Request goes IN */
		CmdBlock->Flags = X86_USB_MSD_CBW_OUT;
		CmdBlock->Length = 10;

		/* Sector Lba */
		CmdBlock->CommandBytes[2] = ((SectorLBA >> 24) & 0xFF);
		CmdBlock->CommandBytes[3] = ((SectorLBA >> 16) & 0xFF);
		CmdBlock->CommandBytes[4] = ((SectorLBA >> 8) & 0xFF);
		CmdBlock->CommandBytes[5] = (SectorLBA & 0xFF);

		/* Sector Count */
		CmdBlock->CommandBytes[7] = ((NumSectors << 8) & 0xFF);
		CmdBlock->CommandBytes[8] = (NumSectors & 0xFF);

	} break;
	}
}

void UsbMsdBuildSCSICommandUFI(uint8_t ScsiCommand,
	MsdCommandBlockWrapUFI_t *CmdBlock, uint64_t SectorLBA, uint32_t DataLen, uint16_t SectorSize)
{
	/* Memset */
	memset((void*)CmdBlock, 0, sizeof(MsdCommandBlockWrapUFI_t));

	/* Set shared */
	CmdBlock->CommandBytes[0] = ScsiCommand;

	/* Now it depends */
	switch (ScsiCommand)
	{
		/* Test Unit Ready - 6 */
		case X86_SCSI_TEST_UNIT_READY:
			break;

		/* Request Sense - 6 */
		case X86_SCSI_REQUEST_SENSE:
		{
			/* Length of allocation */
			CmdBlock->CommandBytes[4] = 18;

		} break;

		/* Inquiry - 6 */
		case X86_SCSI_INQUIRY:
		{
			/* Length of allocation */
			CmdBlock->CommandBytes[4] = 36;

		} break;

		/* Read Capacities - 10 */
		case X86_SCSI_READ_CAPACITY:
		{
			/* Sector Lba */
			CmdBlock->CommandBytes[2] = ((SectorLBA >> 24) & 0xFF);
			CmdBlock->CommandBytes[3] = ((SectorLBA >> 16) & 0xFF);
			CmdBlock->CommandBytes[4] = ((SectorLBA >> 8) & 0xFF);
			CmdBlock->CommandBytes[5] = (SectorLBA & 0xFF);

		} break;

		/* Read - 10 */
		case X86_SCSI_READ:
		{
			/* Calculate block count */
			uint16_t NumSectors = (uint16_t)(DataLen / SectorSize);
			if (DataLen % SectorSize)
				NumSectors++;

			/* Sector Lba */
			CmdBlock->CommandBytes[2] = ((SectorLBA >> 24) & 0xFF);
			CmdBlock->CommandBytes[3] = ((SectorLBA >> 16) & 0xFF);
			CmdBlock->CommandBytes[4] = ((SectorLBA >> 8) & 0xFF);
			CmdBlock->CommandBytes[5] = (SectorLBA & 0xFF);

			/* Sector Count */
			CmdBlock->CommandBytes[7] = ((NumSectors << 8) & 0xFF);
			CmdBlock->CommandBytes[8] = (NumSectors & 0xFF);

		} break;

		/* Write - 10 */
		case X86_SCSI_WRITE:
		{
			/* Calculate block count */
			uint16_t NumSectors = (uint16_t)(DataLen / SectorSize);
			if (DataLen % SectorSize)
				NumSectors++;

			/* Sector Lba */
			CmdBlock->CommandBytes[2] = ((SectorLBA >> 24) & 0xFF);
			CmdBlock->CommandBytes[3] = ((SectorLBA >> 16) & 0xFF);
			CmdBlock->CommandBytes[4] = ((SectorLBA >> 8) & 0xFF);
			CmdBlock->CommandBytes[5] = (SectorLBA & 0xFF);

			/* Sector Count */
			CmdBlock->CommandBytes[7] = ((NumSectors << 8) & 0xFF);
			CmdBlock->CommandBytes[8] = (NumSectors & 0xFF);

		} break;
	}
}

/* Used for making sure the CSW we get back is valid */
UsbTransferStatus_t UsbMsdSanityCsw(MsdDevice_t *Device, MsdCommandStatusWrap_t *Csw)
{
	/* Start out by checking if a phase error occured, in that case we are screwed */
	if (Csw->Status == X86_USB_MSD_CSW_PHASE_ERROR)
	{
		/* Do a reset-recovery */
		UsbMsdResetRecovery(Device);

		/* return error */
		return TransferInvalidData;
	}

	/* Is Status ok? */
	if (Csw->Status != X86_USB_MSD_CSW_OK)
		return TransferInvalidData;

	/* Is signature ok? */
	if (Csw->Signature != X86_USB_MSD_CSW_OK_SIGNATURE)
		return TransferInvalidData;
		

	/* Is TAG ok? */
	if ((Csw->Tag & 0xFFFFFF00) != X86_USB_MSD_TAG_SIGNATURE)
		return TransferInvalidData;

	/* Everything seems ok */
	return TransferFinished;
}

/* Used for In / Control sends */
UsbTransferStatus_t UsbMsdSendSCSICommandIn(uint8_t ScsiCommand, MsdDevice_t *Device,
	uint64_t SectorLBA, void *Buffer, uint32_t DataLength)
{
	/* Vars we will need */
	UsbHcRequest_t CtrlRequest;
	UsbHcRequest_t DataRequest;
	MsdCommandStatusWrap_t StatusBlock;

	/* Setup the Command Transfer */
	if (!Device->IsUFI)
	{
		/* Setup command */
		MsdCommandBlockWrap_t CommandBlock;
		UsbMsdBuildSCSICommand(ScsiCommand, &CommandBlock, SectorLBA, 
			DataLength, (uint16_t)Device->SectorSize);

		/* Setup Transfer */
		UsbTransactionInit((UsbHc_t*)Device->UsbDevice->HcDriver, &CtrlRequest, BulkTransfer,
			Device->UsbDevice, Device->EpOut);
		UsbTransactionOut((UsbHc_t*)Device->UsbDevice->HcDriver, &CtrlRequest,
			0, &CommandBlock, sizeof(MsdCommandBlockWrap_t));
		UsbTransactionSend((UsbHc_t*)Device->UsbDevice->HcDriver, &CtrlRequest);
		UsbTransactionDestroy((UsbHc_t*)Device->UsbDevice->HcDriver, &CtrlRequest);
	}
	else
	{
		/* Setup UFI command */
		MsdCommandBlockWrapUFI_t UfiCommandBlock;
		UsbMsdBuildSCSICommandUFI(ScsiCommand, &UfiCommandBlock, 
			SectorLBA, DataLength, (uint16_t)Device->SectorSize);

		/* Send */
		CtrlRequest.Status =
			UsbFunctionSendPacket((UsbHc_t*)Device->UsbDevice->HcDriver, Device->UsbDevice->Port,
			&UfiCommandBlock, X86_USB_REQ_TARGET_CLASS | X86_USB_REQ_TARGET_INTERFACE, 
			0, 0, 0, (uint16_t)Device->Interface, sizeof(MsdCommandBlockWrapUFI_t));
	}

	/* Sanity */
	if (CtrlRequest.Status != TransferFinished)
	{
		/* Stall? */
		if (CtrlRequest.Status == TransferStalled)
			UsbMsdResetRecovery(Device);
		
		/* Unfinished */
		return CtrlRequest.Status;
	}

	/* Now we can move on to the Data Transfer */
	UsbTransactionInit((UsbHc_t*)Device->UsbDevice->HcDriver, &DataRequest, BulkTransfer,
		Device->UsbDevice, Device->EpIn);

	/* Is it a data transfer? */
	if (DataLength != 0)
		UsbTransactionIn((UsbHc_t*)Device->UsbDevice->HcDriver, &DataRequest, 0, Buffer, DataLength);
	
	/* If it is not UFI, we need a status-response */
	if (!Device->IsUFI)
		UsbTransactionIn((UsbHc_t*)Device->UsbDevice->HcDriver, &DataRequest, 
		0, &StatusBlock, sizeof(MsdCommandStatusWrap_t));

	/* Send it */
	UsbTransactionSend((UsbHc_t*)Device->UsbDevice->HcDriver, &DataRequest);

	/* Cleanup */
	UsbTransactionDestroy((UsbHc_t*)Device->UsbDevice->HcDriver, &DataRequest);

	/* Sanity */
	if (DataRequest.Status != TransferFinished)
		return DataRequest.Status;

	/* Sanity the CSW (only if is not UFI) */
	if (!Device->IsUFI)
		return UsbMsdSanityCsw(Device, &StatusBlock);
	else
		return DataRequest.Status;
}

/* Used for Out sends */
UsbTransferStatus_t UsbMsdSendSCSICommandOut(uint8_t ScsiCommand, MsdDevice_t *Device,
	uint64_t SectorLBA, void *Buffer, uint32_t DataLength)
{
	/* Vars we will need */
	UsbHcRequest_t DataRequest;
	UsbHcRequest_t StatusRequest;
	MsdCommandStatusWrap_t StatusBlock;

	/* Sanity */
	if (!Device->IsUFI)
	{
		/* Setup command */
		MsdCommandBlockWrap_t CommandBlock;
		UsbMsdBuildSCSICommand(ScsiCommand, &CommandBlock, SectorLBA, DataLength, (uint16_t)Device->SectorSize);

		/* Setup Transfer */
		UsbTransactionInit((UsbHc_t*)Device->UsbDevice->HcDriver, &DataRequest, BulkTransfer,
			Device->UsbDevice, Device->EpOut);
		
		/* Send CBW */
		UsbTransactionOut((UsbHc_t*)Device->UsbDevice->HcDriver, &DataRequest,
			0, &CommandBlock, sizeof(MsdCommandBlockWrap_t));

		/* Send Data */
		UsbTransactionOut((UsbHc_t*)Device->UsbDevice->HcDriver, &DataRequest, 0, Buffer, DataLength);

		/* Gogo! */
		UsbTransactionSend((UsbHc_t*)Device->UsbDevice->HcDriver, &DataRequest);
		UsbTransactionDestroy((UsbHc_t*)Device->UsbDevice->HcDriver, &DataRequest);
	}
	else
	{
		/* Setup UFI command */
		MsdCommandBlockWrapUFI_t UfiCommandBlock;
		UsbMsdBuildSCSICommandUFI(ScsiCommand, &UfiCommandBlock, SectorLBA, DataLength, (uint16_t)Device->SectorSize);

		/* Send Package */
		DataRequest.Status =
			UsbFunctionSendPacket((UsbHc_t*)Device->UsbDevice->HcDriver, Device->UsbDevice->Port,
			&UfiCommandBlock, X86_USB_REQ_TARGET_CLASS | X86_USB_REQ_TARGET_INTERFACE,
			0, 0, 0, (uint16_t)Device->Interface, sizeof(MsdCommandBlockWrapUFI_t));

		/* Sanity */
		if (DataRequest.Status != TransferFinished)
			return DataRequest.Status;

		/* Send Data */
		UsbTransactionInit((UsbHc_t*)Device->UsbDevice->HcDriver, &DataRequest, BulkTransfer,
			Device->UsbDevice, Device->EpOut);
		UsbTransactionOut((UsbHc_t*)Device->UsbDevice->HcDriver, &DataRequest, 0, Buffer, DataLength);
		UsbTransactionSend((UsbHc_t*)Device->UsbDevice->HcDriver, &DataRequest);
		UsbTransactionDestroy((UsbHc_t*)Device->UsbDevice->HcDriver, &DataRequest);

		/* Done */
		return DataRequest.Status;
	}

	/* Sanity */
	if (DataRequest.Status != TransferFinished)
		return DataRequest.Status;

	/* Now we do the StatusTransfer */
	UsbTransactionInit((UsbHc_t*)Device->UsbDevice->HcDriver, &StatusRequest, BulkTransfer,
		Device->UsbDevice, Device->EpIn);
	UsbTransactionOut((UsbHc_t*)Device->UsbDevice->HcDriver, &StatusRequest, 0, 
		&StatusBlock, sizeof(MsdCommandStatusWrap_t));
	UsbTransactionSend((UsbHc_t*)Device->UsbDevice->HcDriver, &StatusRequest);
	UsbTransactionDestroy((UsbHc_t*)Device->UsbDevice->HcDriver, &StatusRequest);

	/* Sanity */
	if (StatusRequest.Status != TransferFinished)
		return StatusRequest.Status;

	/* Sanity the CSW (only if is not UFI) */
	if (!Device->IsUFI)
		return UsbMsdSanityCsw(Device, &StatusBlock);
	else
		return DataRequest.Status;
}

/* A combination of Test-Device-Ready & Requst Sense */
void UsbMsdReadyDevice(MsdDevice_t *Device)
{
	/* Sense Data Buffer */
	ScsiSense_t SenseBlock;

	/* We don't use TDR on UFI's */
	if (Device->IsUFI == 0)
	{
		/* Send the TDR */
		if (UsbMsdSendSCSICommandIn(X86_SCSI_TEST_UNIT_READY, Device, 0, NULL, 0)
			!= TransferFinished)
		{
			/* Damn.. */
			LogFatal("USBM", "Failed to test");
			Device->IsReady = 0;
			return;
		}
	}

	/* Request Sense this time */
	if (UsbMsdSendSCSICommandIn(X86_SCSI_REQUEST_SENSE, Device, 0, &SenseBlock, sizeof(ScsiSense_t))
		!= TransferFinished)
	{
		/* Damn.. */
		LogFatal("USBM", "Failed to sense");
		Device->IsReady = 0;
		return;
	}

	/* Sanity Sense */
	uint32_t ResponseCode = SenseBlock.ResponseStatus & 0x7F;
	uint32_t SenseKey = SenseBlock.Flags & 0xF;

	/* Must be either 0x70, 0x71, 0x72, 0x73 */
	if (ResponseCode >= 0x70 && ResponseCode <= 0x73)
	{
		/* Yay ! */
		LogInformation("USBM", "Sense Status: %s", SenseKeys[SenseKey]);
	}
	else
	{
		/* Damn.. */
		LogFatal("USBM", "Invalid Response Code: 0x%x", ResponseCode);
		Device->IsReady = 0;
		return;
	}

	/* Yay! */
	Device->IsReady = 1;
}

/* Read Capacity of a device */
void UsbMsdReadCapacity(MsdDevice_t *Device, MCoreStorageDevice_t *sDevice)
{
	/* Buffer to store data */
	uint32_t CapBuffer[2];

	/* Send Command */
	if (UsbMsdSendSCSICommandIn(X86_SCSI_READ_CAPACITY, Device, 0, &CapBuffer, 8)
		!= TransferFinished)
		return;

	/* We have to switch byte order, but first,
	 * lets sanity */
	if (CapBuffer[0] == 0xFFFFFFFF && !Device->IsUFI)
	{
		/* CapBuffer16 */
		ScsiExtendedCaps_t ExtendedCaps;

		/* Do extended */
		if (UsbMsdSendSCSICommandIn(X86_SCSI_READ_CAPACITY_16, Device, 0, &ExtendedCaps, sizeof(ScsiExtendedCaps_t))
			!= TransferFinished)
			return;

		/* Reverse Byte Order */
		sDevice->SectorCount = rev64(ExtendedCaps.SectorCount) + 1;
		sDevice->SectorSize = rev32(ExtendedCaps.SectorSize);
		Device->SectorSize = sDevice->SectorSize;

		/* Done */
		return;
	}

	/* Reverse Byte Order */
	sDevice->SectorCount = (uint64_t)rev32(CapBuffer[0]) + 1;
	sDevice->SectorSize = rev32(CapBuffer[1]);
	Device->SectorSize = sDevice->SectorSize;
}

/* Read & Write Sectors */
int UsbMsdReadSectors(void *DevData, uint64_t SectorLBA, void *Buffer, uint32_t BufferLength)
{
	/* Cast */
	MsdDevice_t *Device = (MsdDevice_t*)DevData;

	/* Store result */
	UsbTransferStatus_t Result;

	/* Send */
	Result = UsbMsdSendSCSICommandIn(X86_SCSI_READ, Device, SectorLBA, Buffer, BufferLength);

	/* Sanity */
	if (Result != TransferFinished)
		return -1;
	else
		return 0;
}

int UsbMsdWriteSectors(void *DevData, uint64_t SectorLBA, void *Buffer, uint32_t BufferLength)
{
	/* Cast */
	MsdDevice_t *Device = (MsdDevice_t*)DevData;

	/* Store result */
	UsbTransferStatus_t Result;

	/* Send */
	Result = UsbMsdSendSCSICommandOut(X86_SCSI_WRITE, Device, SectorLBA, Buffer, BufferLength);

	/* Sanity */
	if (Result != TransferFinished)
		return -1;
	else
		return 0;
}
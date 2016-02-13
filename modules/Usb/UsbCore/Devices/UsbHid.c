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
* MollenOS X86-32 USB Core HID Driver
*/

/* Includes */
#include <Module.h>
#include <UsbHid.h>
#include <InputManager.h>
#include <Semaphore.h>
#include <Heap.h>
#include <List.h>
#include <Log.h>

#include <string.h>

/* Prototypes */
void UsbHidParseReportDescriptor(HidDevice_t *Device, uint8_t *ReportData, uint32_t ReportLength);
void UsbHidCallback(void *Device);

/* Collection List Helpers */
void UsbHidCollectionInsertChild(UsbHidReportCollection_t *Collection, 
	UsbHidReportGlobalStats_t *Stats, uint32_t HidType, uint32_t Type, void *Data)
{
	/* Prepare iterator vars */
	UsbHidReportCollectionItem_t *CurrChild;

	/* Allocate a new child */
	UsbHidReportCollectionItem_t *Child =
		(UsbHidReportCollectionItem_t*)kmalloc(sizeof(UsbHidReportCollectionItem_t));
	Child->Type = Type;
	Child->InputType = HidType;
	Child->Data = Data;
	memcpy((void*)(&Child->Stats), Stats, sizeof(UsbHidReportGlobalStats_t));
	Child->Link = NULL;

	/* Insert */
	if (Collection->Childs == NULL)
		Collection->Childs = Child;
	else
	{
		CurrChild = Collection->Childs;

		while (CurrChild->Link)
			CurrChild = CurrChild->Link;
		CurrChild->Link = Child;
	}
}

/* Extracts Bits from buffer */
uint64_t UsbHidGetBits(uint8_t *Buffer, uint32_t BitOffset, uint32_t NumBits)
{
	uint64_t value = 0;
	uint32_t i = 0;
	uint32_t offset = BitOffset;

	while (i < NumBits)
	{
		uint8_t bits = ((offset % 8) + NumBits - i) < 8 ? NumBits % 8 : 8 - (offset % 8);
		
		/* */
		value |= ((Buffer[offset / 8] >> (offset % 8)) & ((1 << bits) - 1)) << i;
		
		/* Skip to next set of bits */
		i += bits;
		
		/* Increase offset */
		offset += bits;
	}

	/* Done */
	return value;
}

/* Initialise Driver for a HID */
void UsbHidInit(UsbHcDevice_t *UsbDevice, int InterfaceIndex)
{
	/* Allocate device stuff */
	HidDevice_t *DevData = (HidDevice_t*)kmalloc(sizeof(HidDevice_t));
	UsbHc_t *UsbHcd = (UsbHc_t*)UsbDevice->HcDriver;

	/* Setup vars 
	 * and prepare for a descriptor loop */
	uint8_t *BufPtr = (uint8_t*)UsbDevice->Descriptors;
	size_t BytesLeft = UsbDevice->DescriptorsLength;

	/* Needed for parsing */
	UsbHidDescriptor_t *HidDescriptor = NULL;
	uint8_t *ReportDescriptor = NULL;
	size_t i;

	/* Locate the HID descriptor */
	while (BytesLeft > 0)
	{
		/* Cast */
		uint8_t Length = *BufPtr;
		uint8_t Type = *(BufPtr + 1);

		/* Is this a HID descriptor ? */
		if (Type == USB_DESCRIPTOR_TYPE_HID
			&& Length == sizeof(UsbHidDescriptor_t))
		{
			HidDescriptor = (UsbHidDescriptor_t*)BufPtr;
			break;
		}
		else
		{
			BufPtr += Length;
			BytesLeft -= Length;
		}
	}

	/* Sanity */
	if (HidDescriptor == NULL)
	{
		LogFatal("USBH", "HID Descriptor did not exist.");
		kfree(DevData);
		return;
	}

	/* Zero structure */
	memset(DevData, 0, sizeof(HidDevice_t));

	/* Locate interrupt IN endpoint 
	 * We only check endpoints related to this
	 * Interface */
	/* Locate neccessary endpoints */
	for (i = 0; i < UsbDevice->Interfaces[InterfaceIndex]->Versions[0]->NumEndpoints; i++)
	{
		/* Interrupt? */
		if (UsbDevice->Interfaces[InterfaceIndex]->Versions[0]->Endpoints[i]->Type == EndpointInterrupt) {
			DevData->EpInterrupt = UsbDevice->Interfaces[InterfaceIndex]->Versions[0]->Endpoints[i];
			break;
		}
	}

	/* Sanity */
	if (DevData->EpInterrupt == NULL)
	{
		LogFatal("USBH", "HID Endpoint (In, Interrupt) did not exist.");
		kfree(DevData);
		return;
	}

	/* Switch to Report Protocol (ONLY if we are in boot protocol) */
	if (UsbDevice->Interfaces[InterfaceIndex]->Class == USB_HID_SUBCLASS_BOOT) {
		UsbFunctionSendPacket((UsbHc_t*)UsbDevice->HcDriver, UsbDevice->Port, 0,
			USB_REQUEST_TARGET_CLASS | USB_REQUEST_TARGET_INTERFACE,
			USB_HID_SET_PROTOCOL, 0, 1, (uint8_t)InterfaceIndex, 0);
	}

	/* Set idle and silence the endpoint unless events */
	/* We might have to set ValueHi to 500 ms for keyboards, but has to be tested
	* time is calculated in 4ms resolution, so 500ms = HiVal = 125 */

	/* This request MAY stall, which means it's unsupported */
	UsbFunctionSendPacket((UsbHc_t*)UsbDevice->HcDriver, UsbDevice->Port, NULL,
		USB_REQUEST_TARGET_CLASS | USB_REQUEST_TARGET_INTERFACE,
		USB_HID_SET_IDLE, 0, 0, (uint8_t)InterfaceIndex, 0);

	/* Get Report Descriptor */
	ReportDescriptor = (uint8_t*)kmalloc(HidDescriptor->ClassDescriptorLength);
	if (UsbFunctionGetDescriptor((UsbHc_t*)UsbDevice->HcDriver, UsbDevice->Port,
		ReportDescriptor, USB_REQUEST_DIR_IN | USB_REQUEST_TARGET_INTERFACE,
		HidDescriptor->ClassDescriptorType,
		0, (uint8_t)InterfaceIndex, HidDescriptor->ClassDescriptorLength) != TransferFinished)
	{
		LogFatal("USBH", "Failed to get Report Descriptor.");
		kfree(ReportDescriptor);
		kfree(DevData);
		return;
	}

	/* Initialise the EP */
	UsbHcd->EndpointSetup(UsbHcd->Hc, DevData->EpInterrupt);

	/* Allocate Interrupt Channel */
	DevData->InterruptChannel = (UsbHcRequest_t*)kmalloc(sizeof(UsbHcRequest_t));
	DevData->InterruptChannel->Callback = 
		(UsbInterruptCallback_t*)kmalloc(sizeof(UsbInterruptCallback_t));

	/* Setup Callback */
	DevData->UsbDevice = UsbDevice;
	DevData->InterruptChannel->Callback->Callback = UsbHidCallback;
	DevData->InterruptChannel->Callback->Args = DevData;

	/* Set driver data */
	DevData->PrevDataBuffer = (uint8_t*)kmalloc(DevData->EpInterrupt->MaxPacketSize);
	DevData->DataBuffer = (uint8_t*)kmalloc(DevData->EpInterrupt->MaxPacketSize);
	DevData->Collection = NULL;

	/* Memset Databuffers */
	memset(DevData->PrevDataBuffer, 0, DevData->EpInterrupt->MaxPacketSize);
	memset(DevData->DataBuffer, 0, DevData->EpInterrupt->MaxPacketSize);

	/* Parse Report Descriptor */
	UsbHidParseReportDescriptor(DevData, ReportDescriptor, HidDescriptor->ClassDescriptorLength);

	/* Debug */
	LogDebug("USBH", "Installing Interrupt Pipe");

	/* Install Interrupt */
	UsbTransactionInit(UsbHcd, DevData->InterruptChannel, 
		InterruptTransfer, UsbDevice, DevData->EpInterrupt);
	UsbTransactionIn(UsbHcd, DevData->InterruptChannel, 0, 
		DevData->DataBuffer, DevData->EpInterrupt->MaxPacketSize);
	UsbTransactionSend(UsbHcd, DevData->InterruptChannel);
}

/* Parses the report descriptor and stores it as 
 * collection tree */
void UsbHidParseReportDescriptor(HidDevice_t *Device, uint8_t *ReportData, size_t ReportLength)
{
	/* Iteration vars */
	size_t i = 0, j = 0, Depth = 0;

	/* Used for collection data */
	UsbHidReportCollection_t *Collection = NULL, *RootCollection = NULL;
	UsbHidReportGlobalStats_t GlobalStats = { 0 };
	UsbHidReportItemStats_t ItemStats = { 0 };

	/* Offsets */
	uint32_t bit_offset = 0;
	uint32_t current_type = MCORE_INPUT_TYPE_UNKNOWN;

	/* Null Report Id */
	GlobalStats.ReportId = 0xFFFFFFFF;

	/* Loop through report */
	for (i = 0; i < ReportLength; /* Increase Manually */)
	{
		/* Bits 0-1 (Must be either 0, 1, 2 or 4) 3 = 4*/
		uint8_t Size = ReportData[i] & 0x03;

		/* Bits 2-3 */
		uint8_t Type = ReportData[i] & 0x0C;

		/* Bits 4-7 */
		uint8_t Tag = ReportData[i] & 0xF0;

		/* The packet */
		uint32_t Packet = 0;

		/* Size Fixup */
		if (Size == 3)
			Size++;

		/* Get actual packet (The byte(s) after the header) */
		if (Size == 1)
			Packet = ReportData[i + 1];
		else if (Size == 2)
			Packet = ReportData[i + 1] | (uint32_t)((ReportData[i + 2] << 8) & 0xFF00);
		else if (Size == 4)
			Packet = ReportData[i + 1] | (uint32_t)((ReportData[i + 2] << 8) & 0xFF00)
			| (uint32_t)((ReportData[i + 3] << 16) & 0xFF0000) 
			| (uint32_t)((ReportData[i + 4] << 24) & 0xFF000000);

		/* Update Report Pointer */
		i += (Size + 1);

		/* Sanity, the first item outside an collection should be an COLLECTION!! */
		if (Collection == NULL 
			&& Type == X86_USB_REPORT_TYPE_MAIN 
			&& Tag != X86_USB_REPORT_MAIN_TAG_COLLECTION)
			continue;

		/* Ok, we have 3 item groups. Main, Local & Global */
		switch (Type)
		{
			/* Main Item */
			case X86_USB_REPORT_TYPE_MAIN:
			{
				/* 5 Main Item Groups: Input, Output, Feature, Collection, EndCollection */
				switch (Tag)
				{
					/* Collection Item */
					case X86_USB_REPORT_MAIN_TAG_COLLECTION:
					{
						/* Create a new collection and set parent */
						UsbHidReportCollection_t *NextCollection =
							(UsbHidReportCollection_t*)kmalloc(sizeof(UsbHidReportCollection_t));
						NextCollection->Childs = NULL;
						NextCollection->NextParent = NULL;

						if (Collection == NULL)
						{
							Collection = NextCollection;
						}
						else
						{
							/* Set parent */
							NextCollection->NextParent = Collection;

							/* Create a child node */
							UsbHidCollectionInsertChild(Collection, &GlobalStats, current_type,
								X86_USB_COLLECTION_TYPE_COLLECTION, NextCollection);

							/* Enter collection */
							Collection = NextCollection;
						}

						/* Increase Depth */
						Depth++;

					} break;

					/* End of Collection Item */
					case X86_USB_REPORT_MAIN_TAG_ENDCOLLECTION:
					{
						/* Move up */
						if (Collection != NULL)
						{
							/* If parent is null, root collection is done (save it) */
							if (Collection->NextParent != NULL)
								RootCollection = Collection;
							Collection = Collection->NextParent;
						}

						/* Decrease Depth */
						Depth--;

					} break;

					/* Input Item */
					case X86_USB_REPORT_MAIN_TAG_INPUT:
					{
						UsbHidReportInputItem_t *InputItem =
							(UsbHidReportInputItem_t*)kmalloc(sizeof(UsbHidReportInputItem_t));

						/* What kind of input data? */
						if (Packet & 0x1)
							InputItem->Flags = X86_USB_REPORT_INPUT_TYPE_CONSTANT;
						else
						{
							/* This is data, not constant */
							if (Packet & 0x2)
							{
								/* Variable Input */
								if (Packet & 0x4)
									InputItem->Flags = X86_USB_REPORT_INPUT_TYPE_RELATIVE; /* Data, Variable, Relative */
								else
									InputItem->Flags = X86_USB_REPORT_INPUT_TYPE_ABSOLUTE; /* Data, Variable, Absolute */
							}
							else
								InputItem->Flags = X86_USB_REPORT_INPUT_TYPE_ARRAY; /* Data, Array */
						}

						/* Set local stats */
						for (j = 0; j < 16; j++)
							InputItem->Stats.Usages[j] = ItemStats.Usages[j];

						InputItem->Stats.UsageMin = ItemStats.UsageMin;
						InputItem->Stats.UsageMax = ItemStats.UsageMax;
						InputItem->Stats.BitOffset = bit_offset;

						/* Add to list */
						UsbHidCollectionInsertChild(Collection, &GlobalStats, current_type,
							X86_USB_COLLECTION_TYPE_INPUT, InputItem);

						/* Increase Bitoffset Counter */
						bit_offset += GlobalStats.ReportCount * GlobalStats.ReportSize;
						
					} break;

					/* Output Item */
					case X86_USB_REPORT_MAIN_TAG_OUTPUT:
					{
						LogInformation("USBH", "%u: Output Item (%u)", Depth, Packet);
					} break;

					/* Feature Item */
					case X86_USB_REPORT_MAIN_TAG_FEATURE:
					{
						LogInformation("USBH", "%u: Feature Item (%u)", Depth, Packet);
					} break;
				}

				/* Reset Local Stats */
				for (j = 0; j < 16; j++)
					ItemStats.Usages[j] = 0;
				ItemStats.UsageMin = 0;
				ItemStats.UsageMax = 0;
				ItemStats.BitOffset = 0;

			} break;

			/* Global Item */
			case X86_USB_REPORT_TYPE_GLOBAL:
			{
				switch (Tag)
				{
					/* Usage Page */
					case X86_USB_REPORT_GLOBAL_TAG_USAGE_PAGE:
					{
						GlobalStats.Usage = Packet;
					} break;

					/* Logical Min & Max */
					case X86_USB_REPORT_GLOBAL_TAG_LOGICAL_MIN:
					{
						/* New pair of log */
						if (GlobalStats.HasLogicalMin != 0)
							GlobalStats.HasLogicalMax = 0;
						
						GlobalStats.LogicalMin = (int32_t)Packet;
						GlobalStats.HasLogicalMin = 1;

						if (GlobalStats.HasLogicalMax != 0)
						{
							/* Make sure minimum value is less than max */
							if ((int)(GlobalStats.LogicalMin) >= (int)(GlobalStats.LogicalMax))
							{
								/* Sign it */
								GlobalStats.LogicalMin = ~(GlobalStats.LogicalMin);
								GlobalStats.LogicalMin++;
							}
						}

					} break;
					case X86_USB_REPORT_GLOBAL_TAG_LOGICAL_MAX:
					{
						/* New pair of log */
						if (GlobalStats.HasLogicalMax != 0)
							GlobalStats.HasLogicalMin = 0;

						GlobalStats.LogicalMax = (int32_t)Packet;
						GlobalStats.HasLogicalMax = 1;

						if (GlobalStats.HasLogicalMin != 0)
						{
							/* Make sure minimum value is less than max */
							if ((int)(GlobalStats.LogicalMin) >= (int)(GlobalStats.LogicalMax))
							{
								/* Sign it */
								GlobalStats.LogicalMin = ~(GlobalStats.LogicalMin);
								GlobalStats.LogicalMin++;
							}
						}

					} break;

					/* Physical Min & Max */
					case X86_USB_REPORT_GLOBAL_TAG_PHYSICAL_MIN:
					{
						/* New pair of physical */
						if (GlobalStats.HasPhysicalMin != 0)
							GlobalStats.HasPhysicalMax = 0;

						GlobalStats.PhysicalMin = (int32_t)Packet;
						GlobalStats.HasPhysicalMin = 1;

						if (GlobalStats.HasPhysicalMax != 0)
						{
							/* Make sure minimum value is less than max */
							if ((int)(GlobalStats.PhysicalMin) >= (int)(GlobalStats.PhysicalMax))
							{
								/* Sign it */
								GlobalStats.PhysicalMin = ~(GlobalStats.PhysicalMin);
								GlobalStats.PhysicalMin++;
							}
						}

					} break;
					case X86_USB_REPORT_GLOBAL_TAG_PHYSICAL_MAX:
					{
						/* New pair of physical */
						if (GlobalStats.HasPhysicalMax != 0)
							GlobalStats.HasPhysicalMin = 0;

						GlobalStats.PhysicalMax = (int32_t)Packet;
						GlobalStats.HasPhysicalMax = 1;

						if (GlobalStats.HasPhysicalMin != 0)
						{
							/* Make sure minimum value is less than max */
							if ((int)(GlobalStats.PhysicalMin) >= (int)(GlobalStats.PhysicalMax))
							{
								/* Sign it */
								GlobalStats.PhysicalMin = ~(GlobalStats.PhysicalMin);
								GlobalStats.PhysicalMin++;
							}
						}

						/* Unit & Unit Exponent */
						case X86_USB_REPORT_GLOBAL_TAG_UNIT_VALUE:
						{
							GlobalStats.UnitType = (int32_t)Packet;
						} break;
						case X86_USB_REPORT_GLOBAL_TAG_UNIT_EXPONENT:
						{
							GlobalStats.UnitExponent = (int32_t)Packet;
						} break;

					} break;

					/* Report Items */
					case X86_USB_REPORT_GLOBAL_TAG_REPORT_ID:
					{
						GlobalStats.ReportId = Packet;
					} break;
					case X86_USB_REPORT_GLOBAL_TAG_REPORT_COUNT:
					{
						GlobalStats.ReportCount = Packet;
					} break;
					case X86_USB_REPORT_GLOBAL_TAG_REPORT_SIZE:
					{
						GlobalStats.ReportSize = Packet;
					} break;
						
					/* Unhandled */
					default:
					{
						LogInformation("USBH", "%u: Global Item %u", Depth, Tag);
					} break;
				}

			} break;

			/* Local Item */
			case X86_USB_REPORT_TYPE_LOCAL:
			{
				switch (Tag)
				{
					/* Usage */
					case X86_USB_REPORT_GLOBAL_TAG_USAGE_PAGE:
					{
						if (Packet == X86_USB_REPORT_USAGE_POINTER
							|| Packet == X86_USB_REPORT_USAGE_MOUSE)
							current_type = MCORE_INPUT_TYPE_MOUSE;
						else if (Packet == X86_USB_REPORT_USAGE_KEYBOARD)
							current_type = MCORE_INPUT_TYPE_KEYBOARD;
						else if (Packet == X86_USB_REPORT_USAGE_KEYPAD)
							current_type = MCORE_INPUT_TYPE_KEYPAD;
						else if (Packet == X86_USB_REPORT_USAGE_JOYSTICK)
							current_type = MCORE_INPUT_TYPE_JOYSTICK;
						else if (Packet == X86_USB_REPORT_USAGE_GAMEPAD)
							current_type = MCORE_INPUT_TYPE_GAMEPAD;

						for (j = 0; j < 16; j++)
						{
							if (ItemStats.Usages[j] == 0)
							{
								ItemStats.Usages[j] = Packet;
								break;
							}
						}
						
					} break;

					/* Uage Min & Max */
					case X86_USB_REPORT_LOCAL_TAG_USAGE_MIN:
					{
						ItemStats.UsageMin = Packet;
					} break;
					case X86_USB_REPORT_LOCAL_TAG_USAGE_MAX:
					{
						ItemStats.UsageMax = Packet;
					} break;

					/* Unhandled */
					default:
					{
						LogInformation("USBH", "%u: Local Item %u", Depth, Tag);
					} break;
				}
			} break;
		}
	}

	/* Save it in driver data */
	Device->Collection = (RootCollection == NULL) ? Collection : RootCollection;
}

/* Gives input data to an input item */
void UsbHidApplyInputData(HidDevice_t *Device, UsbHidReportCollectionItem_t *CollectionItem)
{
	/* Cast */
	UsbHidReportInputItem_t *InputItem = (UsbHidReportInputItem_t*)CollectionItem->Data;

	/* Need those for registrating events */
	MCorePointerEvent_t PointerData = { 0 };
	MCoreButtonEvent_t ButtonData = { 0 };

	/* Set Headers */
	ButtonData.Header.Type = EventInput;
	ButtonData.Header.Length = sizeof(MCoreButtonEvent_t);

	PointerData.Header.Type = EventInput;
	PointerData.Header.Length = sizeof(MCorePointerEvent_t);

	/* And these for parsing */
	uint64_t Value = 0, OldValue = 0;
	size_t i, Offset, Length, Usage;

	/* If we are constant, we are padding :D */
	if (InputItem->Flags == X86_USB_REPORT_INPUT_TYPE_CONSTANT)
		return;

	/* If we have a valid ReportID, make sure this data is for us */
	if (CollectionItem->Stats.ReportId != 0xFFFFFFFF)
	{
		/* The first byte is the report Id */
		uint8_t ReportId = Device->DataBuffer[0];

		/* Sanity */
		if (ReportId != (uint8_t)CollectionItem->Stats.ReportId)
			return;
	}

	/* Set type */
	PointerData.Type = CollectionItem->InputType;
	ButtonData.Type = CollectionItem->InputType;

	/* Loop through report data */
	Offset = InputItem->Stats.BitOffset;
	Length = CollectionItem->Stats.ReportSize;
	for (i = 0; i < CollectionItem->Stats.ReportCount; i++)
	{
		/* Get bits in question! */
		Value = UsbHidGetBits(Device->DataBuffer, Offset, Length);
		OldValue = 0;
		
		/* We cant expect this to be correct though, it might be 0 */
		Usage = InputItem->Stats.Usages[i];

		/* Holy shit i hate switches */
		switch (CollectionItem->Stats.Usage)
		{
			/* Mouse, Keyboard etc */
			case X86_USB_REPORT_USAGE_PAGE_GENERIC_PC:
			{
				/* Lets check sub-type (local usage) */
				switch (Usage)
				{
					/* Calculating Device Bounds 
					 * Resolution = (Logical Maximum – Logical Minimum) / 
					 * ((Physical Maximum – Physical Minimum) * (10 Unit Exponent)) */

					/* If physical min/max is not defined or are 0, we set them to be logical min/max */

					/* X Axis Update */
					case X86_USB_REPORT_USAGE_X_AXIS:
					{
						int64_t xRelative = (int64_t)Value;

						/* Sanity */
						if (Value == 0)
							break;

						/* Convert to relative if necessary */
						if (InputItem->Flags == X86_USB_REPORT_INPUT_TYPE_ABSOLUTE)
						{
							/* Get last value */
							OldValue = UsbHidGetBits(Device->PrevDataBuffer, Offset, Length);
							xRelative = (int64_t)(Value - OldValue);
						}

						/* Fix-up relative number */
						if (xRelative > CollectionItem->Stats.LogicalMax
							&& CollectionItem->Stats.LogicalMin < 0)
						{
							/* This means we have to sign x_relative */
							xRelative = (int64_t)(~((uint64_t)xRelative));
							xRelative++;
						}

						if (xRelative != 0)
						{
							/* Add it */
							PointerData.xRelative = (int32_t)xRelative;

							/* Now it epends on mouse, joystick or w/e */
							LogInformation("USBH", "X-Change: %i (Original 0x%x, Old 0x%x)",
								(int32_t)xRelative, (uint32_t)Value, (uint32_t)OldValue);
						}

					} break;

					/* Y Axis Update */
					case X86_USB_REPORT_USAGE_Y_AXIS:
					{
						int64_t yRelative = (int64_t)Value;

						/* Sanity */
						if (Value == 0)
							break;

						/* Convert to relative if necessary */
						if (InputItem->Flags == X86_USB_REPORT_INPUT_TYPE_ABSOLUTE)
						{
							/* Get last value */
							OldValue = UsbHidGetBits(Device->PrevDataBuffer, Offset, Length);
							yRelative = (int64_t)(Value - OldValue);
						}

						/* Fix-up relative number */
						if (yRelative > CollectionItem->Stats.LogicalMax
							&& CollectionItem->Stats.LogicalMin < 0)
						{
							/* This means we have to sign x_relative */
							yRelative = (int64_t)(~((uint64_t)yRelative));
							yRelative++;
						}

						if (yRelative != 0)
						{
							/* Add it */
							PointerData.yRelative = (int32_t)yRelative;

							/* Now it epends on mouse, joystick or w/e */
							LogInformation("USBH", "Y-Change: %i (Original 0x%x, Old 0x%x)",
								(int32_t)yRelative, (uint32_t)Value, (uint32_t)OldValue);
						}

					} break;

					/* Z Axis Update */
					case X86_USB_REPORT_USAGE_Z_AXIS:
					{
						int64_t zRelative = (int64_t)Value;

						/* Sanity */
						if (Value == 0)
							break;

						/* Convert to relative if necessary */
						if (InputItem->Flags == X86_USB_REPORT_INPUT_TYPE_ABSOLUTE)
						{
							/* Get last value */
							OldValue = UsbHidGetBits(Device->PrevDataBuffer, Offset, Length);
							zRelative = (int64_t)(Value - OldValue);
						}

						/* Fix-up relative number */
						if (zRelative > CollectionItem->Stats.LogicalMax
							&& CollectionItem->Stats.LogicalMin < 0)
						{
							/* This means we have to sign x_relative */
							zRelative = (int64_t)(~((uint64_t)zRelative));
							zRelative++;
						}

						if (zRelative != 0)
						{
							/* Add it */
							PointerData.zRelative = (int32_t)zRelative;

							/* Now it epends on mouse, joystick or w/e */
							LogInformation("USBH", "Z-Change: %i (Original 0x%x, Old 0x%x)",
								(int32_t)zRelative, (uint32_t)Value, (uint32_t)OldValue);
						}

					} break;
				}

			} break;

			/* Keyboard, Keypad */
			case X86_USB_REPORT_USAGE_PAGE_KEYBOARD:
			{

			} break;

			/* Buttons (Mouse, Joystick, Gamepad, etc) */
			case X86_USB_REPORT_USAGE_PAGE_BUTTON:
			{
				/* Determine if keystate has changed */
				uint8_t KeystateChanged = 0;
				OldValue = UsbHidGetBits(Device->PrevDataBuffer, Offset, Length);

				if (Value != OldValue)
					KeystateChanged = 1;
				else
					break;

				/* Ok, so if we have multiple buttons (an array) 
				 * we will use the logical min & max to find out which
				 * button id this is */
				LogInformation("USBH", "Button %u: %u", i, (uint32_t)Value);

				/* Keyboard, keypad, mouse, gamepad or joystick? */
				switch (CollectionItem->InputType)
				{
					/* Mouse Button */
					case MCORE_INPUT_TYPE_MOUSE:
					{

					} break;

					/* Gamepad Button */
					case MCORE_INPUT_TYPE_GAMEPAD:
					{

					} break;

					/* Joystick Button */
					case MCORE_INPUT_TYPE_JOYSTICK:
					{

					} break;

					/* /Care */
					default:
						break;
				}

			} break;

			/* Consumer, this is device-specific */
			case X86_USB_REPORT_USAGE_PAGE_CONSUMER:
			{
				/* Virtual box sends me 0x238 which means AC Pan 
				 * which actually is a kind of scrolling 
				 * From the HID Usage Table Specs:
				 * Sel - Set the horizontal offset of the display in the document. */
			} break;

			/* Debug Print */
			default:
			{
				/* What kind of hat is this ? */
				LogInformation("USBH", "Usage Page 0x%x (Input Type 0x%x), Usage 0x%x, Value 0x%x",
					CollectionItem->Stats.Usage, CollectionItem->InputType, Usage, (uint32_t)Value);
			} break;
		}

		/* Increase offset */
		Offset += Length;
	}
	
	/* We send a report here */
	if (PointerData.xRelative != 0 ||
		PointerData.yRelative != 0 ||
		PointerData.zRelative != 0)
	{
		/* Register data */
		//InputManagerCreatePointerEvent(&PointerData);
	}

	/* DebugPrint("Input Item (%u): Report Offset %u, Report Size %u, Report Count %u (Minimum %i, Maximmum %i)\n",
		input->stats.usages[0], input->stats.bit_offset, input_item->stats.report_size, input_item->stats.report_count,
		input_item->stats.log_min, input_item->stats.log_max); */
}

/* Parses an subcollection, is recursive */
int UsbHidApplyCollectionData(HidDevice_t *Device, UsbHidReportCollection_t *Collection)
{
	/* Vars needed */
	UsbHidReportCollectionItem_t *Itr = Collection->Childs;
	int Calls = 0;
	
	/* Parse */
	while (Itr != NULL)
	{
		switch (Itr->Type)
		{
			/* Sub Collection */
			case X86_USB_COLLECTION_TYPE_COLLECTION:
			{
				UsbHidReportCollection_t *SubCollection
					= (UsbHidReportCollection_t*)Itr->Data;

				/* Sanity */
				if (SubCollection == NULL)
					break;

				/* Recall */
				Calls += UsbHidApplyCollectionData(Device, SubCollection);

			} break;

			/* Input */
			case X86_USB_COLLECTION_TYPE_INPUT:
			{
				/* Parse Input */
				UsbHidApplyInputData(Device, Itr);

				/* Increase */
				Calls++;

			} break;

			default:
				break;
		}

		/* Next Item */
		Itr = Itr->Link;
	}

	return Calls;
}

/* The callback for device-feedback */
void UsbHidCallback(void *Device)
{
	/* Vars */
	HidDevice_t *DevData = (HidDevice_t*)Device;

	/* Sanity */
	if (DevData->Collection == NULL)
		return;

	/* Parse Collection (Recursively) */
	if (!UsbHidApplyCollectionData(DevData, DevData->Collection))
	{
		LogInformation("USBH", "No calls were made...");
		return;
	}

	/* Now store this in old buffer */
	memcpy(DevData->PrevDataBuffer, DevData->DataBuffer, DevData->EpInterrupt->MaxPacketSize);
}
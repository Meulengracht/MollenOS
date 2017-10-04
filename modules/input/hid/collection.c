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
 * MollenOS MCore - Human Input Device Driver (Generic)
 */

/* Includes
 * - System */
#include <os/utils.h>
#include <os/driver/usb.h>
#include "hid.h"

/* Includes
 * - Library */
#include <stddef.h>

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


/* Parses a global report item */
void UsbHidParseGlobalItem(UsbHidReportGlobalStats_t *Stats, uint8_t Tag, uint32_t Value)
{
    switch (Tag)
    {
        /* Usage Page */
        case USB_HID_GLOBAL_USAGE_PAGE:
        {
            Stats->Usage = Value;
        } break;

        /* Logical Min & Max */
        case USB_HID_GLOBAL_LOGICAL_MIN:
        {
            /* New pair of log */
            if (Stats->HasLogicalMin != 0)
                Stats->HasLogicalMax = 0;

            Stats->LogicalMin = (int32_t)Value;
            Stats->HasLogicalMin = 1;

            if (Stats->HasLogicalMax != 0)
            {
                /* Make sure minimum value is less than max */
                if ((int)(Stats->LogicalMin) >= (int)(Stats->LogicalMax))
                {
                    /* Sign it */
                    Stats->LogicalMin = ~(Stats->LogicalMin);
                    Stats->LogicalMin++;
                }
            }

        } break;
        case USB_HID_GLOBAL_LOGICAL_MAX:
        {
            /* New pair of log */
            if (Stats->HasLogicalMax != 0)
                Stats->HasLogicalMin = 0;

            Stats->LogicalMax = (int32_t)Value;
            Stats->HasLogicalMax = 1;

            if (Stats->HasLogicalMin != 0)
            {
                /* Make sure minimum value is less than max */
                if ((int)(Stats->LogicalMin) >= (int)(Stats->LogicalMax))
                {
                    /* Sign it */
                    Stats->LogicalMin = ~(Stats->LogicalMin);
                    Stats->LogicalMin++;
                }
            }

        } break;

        /* Physical Min & Max */
        case USB_HID_GLOBAL_PHYSICAL_MIN:
        {
            /* New pair of physical */
            if (Stats->HasPhysicalMin != 0)
                Stats->HasPhysicalMax = 0;

            Stats->PhysicalMin = (int32_t)Value;
            Stats->HasPhysicalMin = 1;

            if (Stats->HasPhysicalMax != 0)
            {
                /* Make sure minimum value is less than max */
                if ((int)(Stats->PhysicalMin) >= (int)(Stats->PhysicalMax))
                {
                    /* Sign it */
                    Stats->PhysicalMin = ~(Stats->PhysicalMin);
                    Stats->PhysicalMin++;
                }
            }

        } break;
        case USB_HID_GLOBAL_PHYSICAL_MAX:
        {
            /* New pair of physical */
            if (Stats->HasPhysicalMax != 0)
                Stats->HasPhysicalMin = 0;

            Stats->PhysicalMax = (int32_t)Value;
            Stats->HasPhysicalMax = 1;

            if (Stats->HasPhysicalMin != 0)
            {
                /* Make sure minimum value is less than max */
                if ((int)(Stats->PhysicalMin) >= (int)(Stats->PhysicalMax))
                {
                    /* Sign it */
                    Stats->PhysicalMin = ~(Stats->PhysicalMin);
                    Stats->PhysicalMin++;
                }
            }

            /* Unit & Unit Exponent */
        case USB_HID_GLOBAL_UNIT_VALUE:
        {
            Stats->UnitType = (int32_t)Value;
        } break;
        case USB_HID_GLOBAL_UNIT_EXPONENT:
        {
            Stats->UnitExponent = (int32_t)Value;
        } break;

        } break;

        /* Report Items */
        case USB_HID_GLOBAL_REPORT_ID:
        {
            Stats->ReportId = Value;
        } break;
        case USB_HID_GLOBAL_REPORT_COUNT:
        {
            Stats->ReportCount = Value;
        } break;
        case USB_HID_GLOBAL_REPORT_SIZE:
        {
            Stats->ReportSize = Value;
        } break;

        /* Unhandled */
        default:
        {
            LogInformation("USBH", "Global Item %u", Tag);
        } break;
    }
}

/* HidCollectionCreate
 * Allocates a new collection and fills it from the current states. */
UsbHidReportCollection_t*
HidCollectionCreate(
    _In_ UsbHidReportGlobalStats_t *GlobalState, 
    _In_ UsbHidReportItemStats_t *ItemState)
{
    // Allocate a new instance
    UsbHidReportCollection_t *Collection =
        (UsbHidReportCollection_t*)malloc(sizeof(UsbHidReportCollection_t));
    memset(Collection, 0, sizeof(UsbHidReportCollection_t));

    // Set members
    Collection->UsagePage = GlobalState->UsagePage;
    Collection->Usage = ItemState->Usages[0];
    return Collection;
}

/* HidCollectionCreateChild
 * Allocates a new collection item, fills it with data and automatically
 * appends it to the given collection children list. */
void
HidCollectionCreateChild(
    UsbHidReportCollection_t *Collection, 
    UsbHidReportGlobalStats_t *Stats,
    MInputType_t InputType, 
    int CollectionType, 
    void *Item)
{
    // Variables
    UsbHidReportCollectionItem_t *CurrentChild = NULL;
    UsbHidReportCollectionItem_t *Child = NULL;

    // Allocate a new collection item
    Child = (UsbHidReportCollectionItem_t*)malloc(sizeof(UsbHidReportCollectionItem_t));

    // Set initial members
    Child->CollectionType = CollectionType;
    Child->InputType = InputType;
    Child->ItemPointer = Item;
    Child->Link = NULL;

    // Make a local copy of the global active stats
    memcpy(&Child->Stats, Stats, sizeof(UsbHidReportGlobalStats_t));

    // Either insert at root if childs are null or
    // insert at end
    if (Collection->Childs == NULL) {
        Collection->Childs = Child;
    }
    else {
        CurrentChild = Collection->Childs;
        while (CurrentChild->Link) {
            CurrentChild = CurrentChild->Link;
        }
        CurrentChild->Link = Child;
    }
}

/* HidParseReportDescriptor
 * Parses the report descriptor and stores it as collection tree. The size
 * of the largest individual report is returned. */
size_t
HidParseReportDescriptor(
    _In_ HidDevice_t *Device,
    _In_ uint8_t *Descriptor,
    _In_ size_t DescriptorLength)
{
    // Variables
    MInputType_t CurrentType = InputUnknown;
    size_t i = 0, j = 0, Depth = 0;
    size_t LongestReport = 0;
    size_t BitOffset = 0;
    int ReportIdsUsed = 0;

    // Collection buffers and pointers
    UsbHidReportCollection_t *CurrentCollection = NULL, 
                             *RootCollection = NULL;
    UsbHidReportGlobalStats_t GlobalStats = { 0 };
    UsbHidReportItemStats_t ItemStats = { 0 };

    // Make sure we set the report id to not available
    GlobalStats.ReportId = UUID_INVALID;

    // Iterate the report descriptor
    for (i = 0; i < DescriptorLength; /* Increase manually */) {
        // Bits 0-1 (Must be either 0, 1, 2 or 4) 3 = 4
        uint8_t Size = ReportData[i] & 0x03;
        uint8_t Type = ReportData[i] & 0x0C; // Bits 2-3
        uint8_t Tag = ReportData[i] & 0xF0; // Bits 4-7
        uint32_t Packet = 0;

        // Sanitize size, if 3, it must be 4
        if (Size == 3) {
            Size++;
        }

        // Get actual packet (The byte(s) after the header)
        if (Size == 1) {
            Packet = ReportData[i + 1];
        }
        else if (Size == 2) {
            Packet = ReportData[i + 1] 
                | (uint32_t)((ReportData[i + 2] << 8) & 0xFF00);
        }
        else if (Size == 4) {
            Packet = ReportData[i + 1] 
                | (uint32_t)((ReportData[i + 2] << 8) & 0xFF00)
                | (uint32_t)((ReportData[i + 3] << 16) & 0xFF0000) 
                | (uint32_t)((ReportData[i + 4] << 24) & 0xFF000000);
        }

        // Update Report Pointer
        i += (Size + 1);

        // The first item that appears in type main MUST be collection
        // otherwise just skip
        if (CurrentCollection == NULL 
            && Type == HID_REPORT_TYPE_MAIN
            && Tag != HID_MAIN_COLLECTION) {
            continue;
        }

        // The type we encounter can be of these types:
        // Main, Global, Local or LongItem
        switch (Type) {
            
            // Main items describe an upper container of either inputs, outputs
            // features or a collection of items (Tag variable)
            case HID_REPORT_TYPE_MAIN: {
                switch (Tag) {
                    // The collection contains a number of collection items
                    // which can be anything. They usually describe a different
                    // kind of report-collection which means the input device
                    // has a number of configurations
                    case HID_MAIN_COLLECTION: {
                        // Create a collection from the current state-variables
                        UsbHidReportCollection_t *Collection =
                            HidCollectionCreate(&GlobalStats, &ItemStats);

                        // Set it if current is not set
                        // then we don't need to insert it to list
                        if (CurrentCollection == NULL) {
                            CurrentCollection = Collection;
                        }
                        else {
                            // Update the parent of the collection
                            Collection->Parent = CurrentCollection;

                            // Append it as a child note now that we 
                            // aren't a child
                            HidCollectionCreateChild(
                                CurrentCollection, &GlobalStats, CurrentType,
                                HID_TYPE_COLLECTION, Collection);

                            // Step into new collection
                            CurrentCollection = Collection;
                        }

                        // Note the current depth
                        Depth++;
                    } break;

                    // The end of collection marker means our collection is
                    // closed and we should switch to parent collection context
                    case HID_MAIN_ENDCOLLECTION: {
                        if (CurrentCollection != NULL) {
                            // If we finish with root collection, no more!
                            if (CurrentCollection->Parent == NULL) {
                                RootCollection = CurrentCollection;
                            }
                            CurrentCollection = CurrentCollection->Parent;
                        }

                        // Note the current depth
                        Depth--;
                    } break;

                    // Input items can describe any kind of physical input device
                    // as mouse pointers, buttons, joysticks etc. The type of data
                    // we recieve is described as either Constant, Relative, Absolute or Array
                    case HID_MAIN_INPUT: {
                        UsbHidReportInputItem_t *InputItem =
                            (UsbHidReportInputItem_t*)malloc(sizeof(UsbHidReportInputItem_t));

                        // If the constant bit is set it overrides rest
                        // of the bits.
                        if (Packet & 0x1) {
                            InputItem->Flags = HID_REPORT_INPUT_TYPE_CONSTANT;
                        }
                        else {
                            // Ok, so the data available is actual dynamic data, not constant data
                            // Now determine if its variable data or array data
                            if (Packet & 0x2) {
                                // If bit 2 is set, the data is variable and relative,
                                // otherwise the data is variable but absolute
                                if (Packet & 0x4) {
                                    InputItem->Flags = HID_REPORT_INPUT_TYPE_RELATIVE;
                                }
                                else {
                                    InputItem->Flags = HID_REPORT_INPUT_TYPE_ABSOLUTE;
                                }
                            }
                            else {
                                InputItem->Flags = HID_REPORT_INPUT_TYPE_ARRAY;
                            }
                        }

                        // Create a new copy of the current local state that applies
                        // only to this input item. Override BitOffset member
                        memcpy(&InputItem->LocalState, &ItemStats, sizeof(UsbHidReportItemStats_t));
                        InputItem->LocalState.BitOffset = BitOffset;

                        // Append it as a child note now that we 
                        // aren't a child
                        HidCollectionCreateChild(
                            CurrentCollection, &GlobalStats, CurrentType,
                            HID_TYPE_INPUT, InputItem);

                        // Adjust BitOffset now to past this item
                        // and also sanitize current length, make sure we store
                        // the longest report
                        BitOffset += GlobalStats.ReportCount * GlobalStats.ReportSize;
                        if ((GlobalStats.ReportCount * GlobalStats.ReportSize) > LongestReport) {
                            LongestReport = GlobalStats.ReportCount * GlobalStats.ReportSize;
                        }
                    } break;

                    // Output examples could be @todo
                    case HID_MAIN_OUTPUT: {

                    } break;

                    // Feature examples could be @todo
                    case HID_MAIN_FEATURE: {

                    } break;
                }

                // At the end of a collection item we need to 
                // reset the local collection item stats
                for (j = 0; j < 16; j++) {
                    ItemStats.Usages[j] = 0;
                }
                ItemStats.UsageMin = 0;
                ItemStats.UsageMax = 0;
                ItemStats.BitOffset = 0;
            } break;

            /* Global Item */
            case HID_REPORT_TYPE_GLOBAL: {
                UsbHidParseGlobalItem(&GlobalStats, Tag, Packet);
                if (GlobalStats.ReportId != UUID_INVALID) {
                    ReportIdsUsed = 1;
                }
            } break;

            /* Local Item */
            case HID_REPORT_TYPE_LOCAL: {
                switch (Tag) {
                    
                    // The usage tag describes which kind of device we are dealing
                    // with and are usefull for determing how to handle it.
                    case HID_LOCAL_USAGE: {
                        // Determine the kind of input device
                        if (Packet == HID_REPORT_USAGE_POINTER
                            || Packet == HID_REPORT_USAGE_MOUSE) {
                            CurrentType = InputMouse;
                        }
                        else if (Packet == HID_REPORT_USAGE_KEYBOARD) {
                            CurrentType = InputKeyboard;
                        }
                        else if (Packet == HID_REPORT_USAGE_KEYPAD) {
                            CurrentType = InputKeypad;
                        }
                        else if (Packet == HID_REPORT_USAGE_JOYSTICK) {
                            CurrentType = InputJoystick;
                        }
                        else if (Packet == HID_REPORT_USAGE_GAMEPAD) {
                            CurrentType = InputGamePad;
                        }

                        // There can be multiple usages for an descriptor
                        // so store up to 16 usages
                        for (j = 0; j < 16; j++) {
                            if (ItemStats.Usages[j] == 0) {
                                ItemStats.Usages[j] = Packet;
                                break;
                            }
                        }
                    } break;

                    // The usage min and max tells us the boundaries of
                    // the data package
                    case HID_LOCAL_USAGE_MIN: {
                        ItemStats.UsageMin = Packet;
                    } break;
                    case HID_LOCAL_USAGE_MAX: {
                        ItemStats.UsageMax = Packet;
                    } break;

                    // If there is anything we don't handle it's not vital
                    // but we should trace it in case we want to handle it
                    default: {
                        TRACE("%u: Local Item %u", Depth, Tag);
                    } break;
                }
            } break;
        }
    }

    /* Save it in driver data */
    Device->Collection = (RootCollection == NULL) ? Collection : RootCollection;

    /* Done - Add one byte for the report id */
    return DIVUP(LongestReport, 8) + ((ReportIdsUsed == 1) ? 1 : 0);
}

/* Gives input data to an input item */
void
UsbHidApplyInputData(
    HidDevice_t *Device, 
    UsbHidReportCollectionItem_t *CollectionItem)
{
    /* Cast */
    UsbHidReportInputItem_t *InputItem = (UsbHidReportInputItem_t*)CollectionItem->Data;

    /* Need those for registrating events */
    MEventMessageInput_t InputData = { 0 };

    /* Set Headers */
    InputData.Header.Type = EventInput;
    InputData.Header.Length = sizeof(MEventMessageInput_t);

    /* And these for parsing */
    uint64_t Value = 0, OldValue = 0;
    size_t i, Offset, Length, Usage;

    /* If we are constant, we are padding :D */
    if (InputItem->Flags == X86_USB_REPORT_INPUT_TYPE_CONSTANT)
        return;

    /* If we have a valid ReportID, make sure this data is for us */
    if (CollectionItem->Stats.ReportId != UUID_INVALID)
    {
        /* The first byte is the report Id */
        uint8_t ReportId = Device->DataBuffer[0];

        /* Sanity */
        if (ReportId != (uint8_t)CollectionItem->Stats.ReportId)
            return;
    }

    /* Set type */
    InputData.Type = CollectionItem->InputType;

    /* Loop through report data */
    Offset = InputItem->Stats.BitOffset;
    Length = CollectionItem->Stats.ReportSize;
    for (i = 0; i < CollectionItem->Stats.ReportCount; i++)
    {
        /* Get bits in question! */
        Value = UsbHidGetBits(Device->DataBuffer, Offset, Length);
        OldValue = UsbHidGetBits(Device->PrevDataBuffer, Offset, Length);
        
        /* We cant expect this to be correct though, it might be 0 */
        Usage = InputItem->Stats.Usages[i];

        /* Holy shit i hate switches */
        switch (CollectionItem->Stats.Usage)
        {
            /* Mouse, Keyboard etc */
            case USB_HID_USAGE_PAGE_GENERIC_PC:
            {
                /* Lets check sub-type (local usage) */
                switch (Usage)
                {
                    /* Calculating Device Bounds 
                     * Resolution = (Logical Maximum � Logical Minimum) / 
                     * ((Physical Maximum � Physical Minimum) * (10 Unit Exponent)) */

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
                            xRelative = (int64_t)(Value - OldValue);

                        /* Fix-up relative number */
                        if (xRelative > CollectionItem->Stats.LogicalMax
                            && CollectionItem->Stats.LogicalMin < 0)
                        {
                            /* This means we have to sign extend x_relative */
                            if (xRelative & (int64_t)(1 << (Length - 1)))
                                xRelative -= (int64_t)(1 << Length);
                        }

                        if (xRelative != 0)
                        {
                            /* Add it */
                            InputData.xRelative = (int32_t)xRelative;

                            /* Now it epends on mouse, joystick or w/e */
                            LogInformation("USBH", "X-Change: %i (Original 0x%x, Old 0x%x, LogMax %i)",
                                (int32_t)xRelative, (uint32_t)Value, (uint32_t)OldValue, 
                                CollectionItem->Stats.LogicalMax);
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
                            yRelative = (int64_t)(Value - OldValue);

                        /* Fix-up relative number */
                        if (yRelative > CollectionItem->Stats.LogicalMax
                            && CollectionItem->Stats.LogicalMin < 0)
                        {
                            /* This means we have to sign y_relative */
                            if (yRelative & (int64_t)(1 << (Length - 1)))
                                yRelative -= (int64_t)(1 << Length);
                        }

                        if (yRelative != 0)
                        {
                            /* Add it */
                            InputData.yRelative = (int32_t)yRelative;

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
                            zRelative = (int64_t)(Value - OldValue);

                        /* Fix-up relative number */
                        if (zRelative > CollectionItem->Stats.LogicalMax
                            && CollectionItem->Stats.LogicalMin < 0)
                        {
                            /* This means we have to sign x_relative */
                            if (zRelative & (int64_t)(1 << (Length - 1)))
                                zRelative -= (int64_t)(1 << Length);
                        }

                        if (zRelative != 0)
                        {
                            /* Add it */
                            InputData.zRelative = (int32_t)zRelative;

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
                    case InputMouse:
                    {

                    } break;

                    /* Gamepad Button */
                    case InputGamePad:
                    {

                    } break;

                    /* Joystick Button */
                    case InputJoystick:
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
    if (InputData.xRelative != 0 ||
        InputData.yRelative != 0 ||
        InputData.zRelative != 0)
    {
        /* Register data */
        //InputManagerCreatePointerEvent(&PointerData);
    }

    /* DebugPrint("Input Item (%u): Report Offset %u, Report Size %u, Report Count %u (Minimum %i, Maximmum %i)\n",
        input->stats.usages[0], input->stats.bit_offset, input_item->stats.report_size, input_item->stats.report_count,
        input_item->stats.log_min, input_item->stats.log_max); */
}

/* HidParseReport
 * Recursive report-parser that applies the given report-data
 * to the parsed report collection. */
OsStatus_t
HidParseReport(
    _In_ HidDevice_t *Device,
    _In_ size_t DataIndex)
{
    /* Vars needed */
    UsbHidReportCollectionItem_t *Itr = Collection->Childs;
    int Calls = 0;
    
    /* Parse */
    while (Itr != NULL) {
        switch (Itr->Type) {
            case USB_HID_TYPE_COLLECTION: {
                UsbHidReportCollection_t *SubCollection = 
                    (UsbHidReportCollection_t*)Itr->Data;
                
                if (SubCollection == NULL) {
                    break;
                }
                
                Calls += HidParseReport(Device, SubCollection);
            } break;
            
            case USB_HID_TYPE_INPUT: {
                UsbHidApplyInputData(Device, Itr);
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

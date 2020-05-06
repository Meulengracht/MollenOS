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
//#define __TRACE

#include "hid.h"
#include <usb/usb.h>
#include <ddk/utils.h>
#include <stdlib.h>

/* HidExtractValue
 * Retrieves a value from a buffer by the given bit-offset and the for a certain
 * number of bits, the extracted value will be treated unsigned */
uint64_t
HidExtractValue(
    _In_ uint8_t *Buffer, 
    _In_ uint32_t BitOffset, 
    _In_ uint32_t NumBits)
{
    // Variables
    uint64_t value = 0;
    uint32_t i = 0;
    uint32_t offset = BitOffset;

    // Do it by loop
    while (i < NumBits) {
        uint8_t bits = ((offset % 8) + NumBits - i) < 8 ? NumBits % 8 : 8 - (offset % 8);
        value |= ((Buffer[offset / 8] >> (offset % 8)) & ((1 << bits) - 1)) << i;
        i += bits;
        offset += bits;
    }

    // Return calculated value
    return value;
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
    _In_ UsbHidReportCollection_t *Collection, 
    _In_ UsbHidReportGlobalStats_t *Stats,
    _In_ DeviceInputType_t InputType, 
    _In_ int CollectionType, 
    _In_ void *Item)
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

/* HidParseGlobalState
 * Parses an Global collection item and extracts all the stored settings
 * in the given GlobalStats structure. */
void
HidParseGlobalState(
    _InOut_ UsbHidReportGlobalStats_t *Stats,
    _In_ uint8_t Tag,
    _In_ uint32_t Value)
{
    switch (Tag) {
        // The usage page is the most frequently data appearing in collections
        // they describe the type of device.
        case HID_GLOBAL_USAGE_PAGE: {
            Stats->UsagePage = Value;
        } break;

        // The logical minimum setting, describes the
        // lowest logical value to expect
        case HID_GLOBAL_LOGICAL_MIN: {
            // Detect new pairs of logical-min/max
            if (Stats->HasLogicalMin != 0) {
                Stats->HasLogicalMax = 0;
            }

            // Store the value and mark its presence
            Stats->LogicalMin = (int32_t)Value;
            Stats->HasLogicalMin = 1;

            // If we have it's counter-part we have to sanitize
            // it's value, because if low-part is higher/equal to
            // higher part, then we have to negate the value by
            // signing it. (It means the low-part is negative)
            if (Stats->HasLogicalMax != 0) {
                if ((int)(Stats->LogicalMin) >= (int)(Stats->LogicalMax)) {
                    Stats->LogicalMin = ~(Stats->LogicalMin);
                    Stats->LogicalMin++;
                }
            }
        } break;

        // The logical maximum setting, describes the
        // highest logical value to expect
        case HID_GLOBAL_LOGICAL_MAX: {
            // Detect new pairs of logical-min/max
            if (Stats->HasLogicalMax != 0) {
                Stats->HasLogicalMin = 0;
            }

            // Store the value and mark its presence
            Stats->LogicalMax = (int32_t)Value;
            Stats->HasLogicalMax = 1;

            // If we have it's counter-part we have to sanitize
            // it's value, because if low-part is higher/equal to
            // higher part, then we have to negate the value by
            // signing it. (It means the low-part is negative)
            if (Stats->HasLogicalMin != 0) {
                if ((int)(Stats->LogicalMin) >= (int)(Stats->LogicalMax)) {
                    Stats->LogicalMin = ~(Stats->LogicalMin);
                    Stats->LogicalMin++;
                }
            }
        } break;

        // The physical minimum setting, describes the
        // lowest phyiscal value to expect
        case HID_GLOBAL_PHYSICAL_MIN: {
            // Detect new pairs of physical-min/max
            if (Stats->HasPhysicalMin != 0) {
                Stats->HasPhysicalMax = 0;
            }

            // Store the value and mark its presence
            Stats->PhysicalMin = (int32_t)Value;
            Stats->HasPhysicalMin = 1;

            // If we have it's counter-part we have to sanitize
            // it's value, because if low-part is higher/equal to
            // higher part, then we have to negate the value by
            // signing it. (It means the low-part is negative)
            if (Stats->HasPhysicalMax != 0) {
                if ((int)(Stats->PhysicalMin) >= (int)(Stats->PhysicalMax)) {
                    Stats->PhysicalMin = ~(Stats->PhysicalMin);
                    Stats->PhysicalMin++;
                }
            }
        } break;

        // The physical maximum setting, describes the
        // highest phyiscal value to expect
        case HID_GLOBAL_PHYSICAL_MAX: {
            // Detect new pairs of physical-min/max
            if (Stats->HasPhysicalMax != 0) {
                Stats->HasPhysicalMin = 0;
            }

            // Store the value and mark its presence
            Stats->PhysicalMax = (int32_t)Value;
            Stats->HasPhysicalMax = 1;

            // If we have it's counter-part we have to sanitize
            // it's value, because if low-part is higher/equal to
            // higher part, then we have to negate the value by
            // signing it. (It means the low-part is negative)
            if (Stats->HasPhysicalMin != 0) {
                if ((int)(Stats->PhysicalMin) >= (int)(Stats->PhysicalMax)) {
                    Stats->PhysicalMin = ~(Stats->PhysicalMin);
                    Stats->PhysicalMin++;
                }
            }
        } break;

        // The Unit & Unit Exponent describes the type of data-unit
        // we can expect from the device, or this given input/output.
        // and if there is an exponent attached we need that for the calculations
        case HID_GLOBAL_UNIT_VALUE: {
            Stats->UnitType = (int32_t)Value;
        } break;
        case HID_GLOBAL_UNIT_EXPONENT: {
            Stats->UnitExponent = (int32_t)Value;
        } break;

        // Report items like Id, Count and Size tells about the actual
        // report that we recieve from the device under data-transfers.
        // These must be present if the device can send us reports. (Except Id)
        case HID_GLOBAL_REPORT_ID: {
            Stats->ReportId = Value;
        } break;
        case HID_GLOBAL_REPORT_COUNT: {
            Stats->ReportCount = Value;
        } break;
        case HID_GLOBAL_REPORT_SIZE: {
            Stats->ReportSize = Value;
        } break;

        // If there is anything we don't handle it's not vital
        // but we should trace it in case we want to handle it
        default: {
            TRACE("Global Item %u", Tag);
        } break;
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
    DeviceInputType_t CurrentType = DeviceInputPointer;
    size_t i = 0, j = 0, Depth = 0;
    size_t LongestReport = 0;
    size_t BitOffset = 0;
    int ReportIdsUsed = 0;

    // Collection buffers and pointers
    UsbHidReportCollection_t *CurrentCollection = NULL, 
                             *RootCollection = NULL;
    UsbHidReportGlobalStats_t GlobalStats = { 0 };
    UsbHidReportItemStats_t ItemStats = { { 0 }, 0 };

    // Make sure we set the report id to not available
    GlobalStats.ReportId = UUID_INVALID;

    // Iterate the report descriptor
    for (i = 0; i < DescriptorLength; /* Increase manually */) {
        // Bits 0-1 (Must be either 0, 1, 2 or 4) 3 = 4
        uint8_t Size = Descriptor[i] & 0x03;
        uint8_t Type = Descriptor[i] & 0x0C; // Bits 2-3
        uint8_t Tag = Descriptor[i] & 0xF0; // Bits 4-7
        uint32_t Packet = 0;

        // Sanitize size, if 3, it must be 4
        if (Size == 3) {
            Size++;
        }

        // Get actual packet (The byte(s) after the header)
        if (Size == 1) {
            Packet = Descriptor[i + 1];
        }
        else if (Size == 2) {
            Packet = Descriptor[i + 1] 
                | (uint32_t)((Descriptor[i + 2] << 8) & 0xFF00);
        }
        else if (Size == 4) {
            Packet = Descriptor[i + 1] 
                | (uint32_t)((Descriptor[i + 2] << 8) & 0xFF00)
                | (uint32_t)((Descriptor[i + 3] << 16) & 0xFF0000) 
                | (uint32_t)((Descriptor[i + 4] << 24) & 0xFF000000);
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
                            InputItem->Flags = REPORT_INPUT_TYPE_CONSTANT;
                        }
                        else {
                            // Ok, so the data available is actual dynamic data, not constant data
                            // Now determine if its variable data or array data
                            if (Packet & 0x2) {
                                // If bit 2 is set, the data is variable and relative,
                                // otherwise the data is variable but absolute
                                if (Packet & 0x4) {
                                    InputItem->Flags = REPORT_INPUT_TYPE_RELATIVE;
                                }
                                else {
                                    InputItem->Flags = REPORT_INPUT_TYPE_ABSOLUTE;
                                }
                            }
                            else {
                                InputItem->Flags = REPORT_INPUT_TYPE_ARRAY;
                            }
                        }

                        // Debug
                        TRACE("Input type %u at bit-offset %u, with data-size in bits %u", 
                            InputItem->Flags, BitOffset, (GlobalStats.ReportCount * GlobalStats.ReportSize));

                        // Create a new copy of the current local state that applies
                        // only to this input item. Override BitOffset member
                        memcpy(&InputItem->LocalState, &ItemStats, 
                            sizeof(UsbHidReportItemStats_t));
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

            // Global items are actually a global state for the entire collection
            // and contains settings that are applied to all children elements
            // They can also carry a report-id which means they only apply to a given
            // report
            case HID_REPORT_TYPE_GLOBAL: {
                HidParseGlobalState(&GlobalStats, Tag, Packet);
                if (GlobalStats.ReportId != UUID_INVALID) {
                    ReportIdsUsed = 1;
                }
            } break;

            // Local items are a local state that only applies to items in the current
            // collection, and thus are reset between major items.
            case HID_REPORT_TYPE_LOCAL: {
                switch (Tag) {
                    
                    // The usage tag describes which kind of device we are dealing
                    // with and are usefull for determing how to handle it.
                    case HID_LOCAL_USAGE: {
                        // Determine the kind of input device
                        if (Packet == HID_REPORT_USAGE_POINTER
                            || Packet == HID_REPORT_USAGE_MOUSE) {
                            CurrentType = DeviceInputPointer;
                        }
                        else if (Packet == HID_REPORT_USAGE_KEYBOARD) {
                            CurrentType = DeviceInputKeyboard;
                        }
                        else if (Packet == HID_REPORT_USAGE_KEYPAD) {
                            CurrentType = DeviceInputKeypad;
                        }
                        else if (Packet == HID_REPORT_USAGE_JOYSTICK) {
                            CurrentType = DeviceInputJoystick;
                        }
                        else if (Packet == HID_REPORT_USAGE_GAMEPAD) {
                            CurrentType = DeviceInputGamePad;
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

    // Store the collection in the device
    // and return the calculated number of maximum bytes reports can use
    Device->Collection = (RootCollection == NULL) ? CurrentCollection : RootCollection;
    if (ReportIdsUsed) {
        return DIVUP(LongestReport, 8) + 1;
    }
    else {
        return DIVUP(BitOffset, 8);
    }
}

/* HidCollectionDestroy
 * Iteratively cleans up a collection and it's subitems. This call
 * is recursive. */
OsStatus_t
HidCollectionDestroy(
    _In_ UsbHidReportCollection_t *Collection)
{
    // Variables
    UsbHidReportCollectionItem_t *ChildIterator = NULL, *Temporary = NULL;

    // Sanitize
    if (Collection == NULL) {
        return OsError;
    }

    // Iterate it's children
    ChildIterator = Collection->Childs;
    while (ChildIterator != NULL) {
        switch (ChildIterator->CollectionType) {
            case HID_TYPE_COLLECTION: {
                HidCollectionDestroy((UsbHidReportCollection_t*)ChildIterator->ItemPointer);
            } break;
            default: {
                free(ChildIterator->ItemPointer);
            }
        }

        // Switch link, free iterator
        Temporary = ChildIterator;
        ChildIterator = ChildIterator->Link;
        free(Temporary);
    }

    // Last step is to free the given collection
    free(Collection);
    return OsSuccess;
}

/* HidCollectionCleanup
 * Cleans up any resources allocated by the collection parser. */
OsStatus_t
HidCollectionCleanup(
    _In_ HidDevice_t *Device)
{
    // Sanitize input
    if (Device == NULL) {
        return OsError;
    }

    // Recursively cleanup
    return HidCollectionDestroy(Device->Collection);
}

/* HidParseReportInput
 * Handles report data from a collection-item of the type input.
 * This means we have actual input data from the device */
void
HidParseReportInput(
    _In_ HidDevice_t *Device,
    _In_ UsbHidReportCollectionItem_t *CollectionItem,
    _In_ size_t DataIndex)
{
    // Variables
    uint8_t *DataPointer = NULL, *PreviousDataPointer = NULL;
    UsbHidReportInputItem_t *InputItem = NULL;
    uint64_t Value = 0, OldValue = 0;
    size_t i, Offset, Length, Usage;

    // Static buffers
    SystemInput_t InputData = { 0 };

    // Cast the input-item from the ItemPointer
    InputItem = (UsbHidReportInputItem_t*)CollectionItem->ItemPointer;

    // Initiate pointers to new and previous data
    DataPointer = &((uint8_t*)Device->Buffer)[DataIndex];
    PreviousDataPointer = &((uint8_t*)Device->Buffer)[Device->PreviousDataIndex];

    // Sanitize the type of input, if we are constant, it's padding
    if (InputItem->Flags == REPORT_INPUT_TYPE_CONSTANT) {
        return;
    }

    // If report-ids are active, we must make sure this data-packet
    // is actually for this report
    // The first byte of the data-report is the id
    if (CollectionItem->Stats.ReportId != UUID_INVALID) {
        uint8_t ReportId = DataPointer[0];
        if (ReportId != (uint8_t)CollectionItem->Stats.ReportId) {
            return;
        }
    }

    // Initiate the mInputEvent data
    InputData.Type = CollectionItem->InputType;

    // Extract some of the state variables for parsing
    Offset = InputItem->LocalState.BitOffset;
    Length = CollectionItem->Stats.ReportSize;

    // Iterate the data
    for (i = 0; i < CollectionItem->Stats.ReportCount; i++, Offset += Length) {
        Value = HidExtractValue(DataPointer, Offset, Length);
        OldValue = HidExtractValue(PreviousDataPointer, Offset, Length);
        
        // We cant expect this to be correct though, it might be 0
        Usage = InputItem->LocalState.Usages[i];

        // Take action based on the type of input
        // currently we only handle generic pc input devices
        switch (CollectionItem->Stats.UsagePage) {
            case HID_USAGE_PAGE_GENERIC_PC: {
                switch (Usage) {
                    // Calculating Device Bounds 
                    // Resolution = (Logical Maximum � Logical Minimum) / 
                    // ((Physical Maximum � Physical Minimum) * (10 Unit Exponent))

                    // If physical min/max is not defined or are 0, 
                    // we set them to be logical min/max

                    // Grid updates like x, y or z coordinates have
                    // changed. 
                    case HID_REPORT_USAGE_X_AXIS:
                    case HID_REPORT_USAGE_Y_AXIS:
                    case HID_REPORT_USAGE_Z_AXIS: {
                        // Variables
                        int64_t Relative = (int64_t)Value;

                        // Sanitize against no changes
                        if (Value == 0) {
                            break;
                        }

                        // If the value is absolute, we want to
                        // make sure we calculate the relative
                        if (InputItem->Flags == REPORT_INPUT_TYPE_ABSOLUTE) {
                            Relative = (int64_t)(Value - OldValue);
                        }

                        // Handle sign-cases where we have to turn them negative
                        if (Relative > CollectionItem->Stats.LogicalMax
                            && CollectionItem->Stats.LogicalMin < 0) {
                            if (Relative & (int64_t)(1 << (Length - 1))) {
                                Relative -= (int64_t)(1 << Length);
                            }
                        }

                        // Guard against relative = 0
                        if (Relative != 0) {
                            char *DebugAxis = NULL;
                            if (Usage == HID_REPORT_USAGE_X_AXIS) {
                                DebugAxis = "X";
                                InputData.RelativeX = (int16_t)(Relative & 0xFFFF);
                            }
                            else if (Usage == HID_REPORT_USAGE_Y_AXIS) {
                                DebugAxis = "Y";
                                InputData.RelativeY = (int16_t)(Relative & 0xFFFF);
                            }
                            else { // HID_REPORT_USAGE_Z_AXIS
                                DebugAxis = "Z";
                                InputData.RelativeZ = (int16_t)(Relative & 0xFFFF);
                            }
                            
                            // Debug
                            TRACE("%s-Change: %i (Original 0x%x, Old 0x%x, LogMax %i)",
                                DebugAxis, (int32_t)Relative, (uint32_t)Value, (uint32_t)OldValue, 
                                CollectionItem->Stats.LogicalMax);
                        }

                    } break;
                }
            } break;

            // Describes keyboard or keypad events
            case HID_REPORT_USAGE_PAGE_KEYBOARD: {

            } break;

            // Generic Button events
            case HID_REPORT_USAGE_PAGE_BUTTON: {
                uint8_t KeystateChanged = 0;

                // Check against old values if any changes are neccessary
                if (Value != OldValue) {
                    KeystateChanged = 1;
                }
                else {
                    break;
                }

                // Ok, so if we have multiple buttons (an array) 
                // we will use the logical min & max to find out which
                // button id this is
                TRACE("Button %u: %u", i, (uint32_t)Value);

                // Possible types are: Keyboard, keypad, mouse, gamepad or joystick
                switch (CollectionItem->InputType) {
                    // Mouse button event
                    case DeviceInputPointer: {

                    } break;

                    // Gamepad button event
                    case DeviceInputGamePad: {

                    } break;

                    // Joystick button event
                    case DeviceInputJoystick: {

                    } break;

                    // Ignore the rest of the input-types
                    default:
                        break;
                }
            } break;

            // Consumer, this is device-specific
            case HID_REPORT_USAGE_PAGE_CONSUMER: {
                // Virtual box sends me 0x238 which means AC Pan 
                // which actually is a kind of scrolling 
                // From the HID Usage Table Specs:
                // Sel - Set the horizontal offset of the display in the document. 
            } break;

            // We don't handle rest of usage-pages, but should be ok
            default: {
                TRACE("Usage Page 0x%x (Input Type 0x%x), Usage 0x%x, Value 0x%x",
                    CollectionItem->Stats.UsagePage, CollectionItem->InputType, Usage, (uint32_t)Value);
            } break;
        }
    }
    
    // Create a new input report
    // @todo

    // Buttons @todo
}

/* HidParseReport
 * Recursive report-parser that applies the given report-data
 * to the parsed report collection. */
OsStatus_t
HidParseReport(
    _In_ HidDevice_t *Device,
    _In_ UsbHidReportCollection_t *Collection,
    _In_ size_t DataIndex)
{
    // Variables
    UsbHidReportCollectionItem_t *Itr = NULL;
    int Calls = 0;

    // Get the collection pointer
    Itr = Collection->Childs;
    
    // Iterate over all the children elements of root
    while (Itr != NULL) {
        switch (Itr->CollectionType) {

            // Collections inside collections must be parsed
            // recursively, so handle them
            case HID_TYPE_COLLECTION: {
                // Sanitize data attached
                if (Itr->ItemPointer == NULL) {
                    break;
                }
                
                // Recursive parser for sub-collections
                Calls += HidParseReport(Device, 
                    (UsbHidReportCollection_t*)Itr->ItemPointer, DataIndex);
            } break;
            
            // Input reports are interesting, that means we have an input event
            case HID_TYPE_INPUT: {
                HidParseReportInput(Device, Itr, DataIndex);
                Calls++;
            } break;

            // For now we don't handle feature-reports
            // output reports are not handled here, but never should
            default:
                break;
        }
        
        // Go to next collection item in the collection
        Itr = Itr->Link;
    }

    // Return the number of actual parses we made
    return Calls;
}

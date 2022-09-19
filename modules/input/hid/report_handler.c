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
 * Human Input Device Driver (Generic)
 */

#define __TRACE

#include "hid.h"
#include <ddk/utils.h>
#include <stdlib.h>

#include <ctt_input_service_server.h>

extern gracht_server_t* __crt_get_module_server(void);

/**
 * Retrieves a value from a buffer by the given bit-offset and the for a certain
 * number of bits, the extracted value will be treated unsigned
 */
static uint64_t __ExtractValue(
        _In_ const uint8_t* buffer,
        _In_ uint32_t       bitOffset,
        _In_ uint32_t       numBits)
{
    uint64_t value = 0;
    uint32_t i = 0;
    uint32_t offset = bitOffset;

    while (i < numBits) {
        uint8_t bits = ((offset % 8) + numBits - i) < 8 ? numBits % 8 : 8 - (offset % 8);
        value |= ((buffer[offset / 8] >> (offset % 8)) & ((1 << bits) - 1)) << i;
        i += bits;
        offset += bits;
    }
    return value;
}

struct ReportHandleContext {
    UsbHidReportCollectionItem_t* CollectionItem;
    UsbHidReportInputItem_t*      InputItem;
    union {
        struct {
            int16_t rel_x;
            int16_t rel_y;
            int16_t rel_z;
        } PointerEvent;
        struct {
            uint8_t  keycode;
            uint16_t modifiers;
        } ButtonEvent;
    } EventData;
};

static void __HandleInputUsageGenericPc(
        _In_ struct ReportHandleContext* context,
        _In_ int                         usage,
        _In_ uint64_t                    oldValue,
        _In_ uint64_t                    value,
        _In_ uint32_t                    numberOfValueBits)
{
    TRACE("__HandleInputUsageGenericPc(usage=%i, oldValue=0x%" PRIxIN ", value=%" PRIxIN ", length=%u)",
          usage, oldValue, value, numberOfValueBits);

    switch (usage) {
        // Calculating Device Bounds
        // Resolution = (Logical Maximum � Logical Minimum) /
        // ((Physical Maximum � Physical Minimum) * (10 Unit Exponent))

        // If physical min/max is not defined or are 0,
        // we set them to be logical min/max

        // Grid updates like x, y or z coordinates have changed.
        case HID_REPORT_USAGE_X_AXIS:
        case HID_REPORT_USAGE_Y_AXIS:
        case HID_REPORT_USAGE_Z_AXIS: {
            int64_t relativeValue = (int64_t)value;
            if (value == 0) {
                break;
            }

            // If the value is absolute, we want to
            // make sure we calculate the relative
            if (context->InputItem->Flags == REPORT_INPUT_TYPE_ABSOLUTE) {
                relativeValue = (int64_t)(value - oldValue);
            }

            // Handle sign-cases where we have to turn them negative
            if (relativeValue > context->CollectionItem->Stats.LogicalMax
                && context->CollectionItem->Stats.LogicalMin < 0) {
                if (relativeValue & (int64_t)(1 << (numberOfValueBits - 1))) {
                    relativeValue -= (int64_t)(1 << numberOfValueBits);
                }
            }

            if (relativeValue != 0) {
                char* debugAxis;
                if (usage == HID_REPORT_USAGE_X_AXIS) {
                    debugAxis = "X";
                    context->EventData.PointerEvent.rel_x = (int16_t)(relativeValue & 0xFFFF);
                }
                else if (usage == HID_REPORT_USAGE_Y_AXIS) {
                    debugAxis = "Y";
                    context->EventData.PointerEvent.rel_y = (int16_t)(relativeValue & 0xFFFF);
                }
                else { // HID_REPORT_USAGE_Z_AXIS
                    debugAxis = "Z";
                    context->EventData.PointerEvent.rel_z = (int16_t)(relativeValue & 0xFFFF);
                }

                TRACE("%s-Change: %i (Original 0x%x, Old 0x%x, LogMax %i)",
                      debugAxis, (int32_t)relativeValue, (uint32_t)value, (uint32_t)oldValue,
                      context->CollectionItem->Stats.LogicalMax);
            }

        } break;

        default: break;
    }
}

static void __HandleInputItem(
        _In_ HidDevice_t*                  hidDevice,
        _In_ UsbHidReportCollectionItem_t* collectionItem,
        _In_ size_t                        dataIndex)
{
    size_t                     i;
    uint32_t                   offset;
    uint32_t                   length;
    UsbHidReportInputItem_t*   inputItem;
    uint8_t*                   previousData;
    uint8_t*                   data;
    struct ReportHandleContext context = { 0 };
    TRACE("__HandleInputItem(hidDevice=0x%" PRIxIN ", collectionItem=0x%" PRIxIN ", dataIndex=%" PRIuIN ")",
          hidDevice, collectionItem, dataIndex);

    inputItem = (UsbHidReportInputItem_t*)collectionItem->ItemPointer;
    // Sanitize the type of input, if we are constant, it's padding
    if (inputItem->Flags == REPORT_INPUT_TYPE_CONSTANT) {
        return;
    }

    data         = &((uint8_t*)hidDevice->Buffer)[dataIndex];
    previousData = &((uint8_t*)hidDevice->Buffer)[hidDevice->PreviousDataIndex];


    // If report-ids are active, we must make sure this data-packet
    // is actually for this report
    // The first byte of the data-report is the id
    if (collectionItem->Stats.ReportId != UUID_INVALID) {
        uint8_t reportId = data[0];
        if (reportId != (uint8_t)collectionItem->Stats.ReportId) {
            return;
        }
    }

    // Extract some of the state variables for parsing
    offset = inputItem->LocalState.BitOffset;
    length = collectionItem->Stats.ReportSize;

    // initialize context before handling
    context.CollectionItem = collectionItem;
    context.InputItem = inputItem;
    for (i = 0; i < collectionItem->Stats.ReportCount; i++, offset += length) {
        uint64_t value    = __ExtractValue(data, offset, length);
        uint64_t oldValue = __ExtractValue(previousData, offset, length);

        // We cant expect this to be correct though, it might be 0
        int usage = inputItem->LocalState.Usages[i];

        // Take action based on the type of input
        // currently we only handle generic pc input devices
        switch (collectionItem->Stats.UsagePage) {
            case HID_USAGE_PAGE_GENERIC_PC: {
                __HandleInputUsageGenericPc(&context, usage, oldValue, value, length);
            } break;

            // Describes keyboard or keypad events
            // See values in hid_keycodes.h
            case HID_REPORT_USAGE_PAGE_KEYBOARD: {

            } break;

            // Generic button event (Mouse)
            // Possible values go through 1..65535 (determined by logical min/max)
            case HID_REPORT_USAGE_PAGE_BUTTON: {
                uint8_t keystateChanged = 0;

                // Check against old values if any changes are neccessary
                if (value != oldValue) {
                    keystateChanged = 1;
                }
                else {
                    break;
                }

                // Ok, so if we have multiple buttons (an array)
                // we will use the logical min & max to find out which
                // button id this is
                TRACE("Button %u: %u", i, (uint32_t)value);

                // Possible types are: Keyboard, keypad, mouse, gamepad or joystick
                switch (collectionItem->InputType) {
                    // Mouse button event
                    case CTT_INPUT_TYPE_MOUSE: {

                    } break;

                    // Gamepad button event
                    case CTT_INPUT_TYPE_GAMEPAD: {

                    } break;

                    // Joystick button event
                    case CTT_INPUT_TYPE_JOYSTICK: {

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
                      collectionItem->Stats.UsagePage, collectionItem->InputType, usage, (uint32_t)value);
            } break;
        }
    }

    // Create a new input report
    if (collectionItem->InputType == CTT_INPUT_TYPE_KEYBOARD) {
        ctt_input_event_button_event_all(__crt_get_module_server(), hidDevice->Base->Base.Id, 0, 0);
    }
    else if (collectionItem->InputType == CTT_INPUT_TYPE_MOUSE) {
        ctt_input_event_cursor_event_all(__crt_get_module_server(), hidDevice->Base->Base.Id, 0,
                                   context.EventData.PointerEvent.rel_x,
                                   context.EventData.PointerEvent.rel_y,
                                   context.EventData.PointerEvent.rel_z);
    }
}

int
HidHandleReportEvent(
        _In_ HidDevice_t*              hidDevice,
        _In_ UsbHidReportCollection_t* reportCollection,
        _In_ size_t                    dataIndex)
{
    UsbHidReportCollectionItem_t* itr;
    int                           calls = 0;
    TRACE("HidHandleReportEvent(hidDevice=0x%" PRIxIN ", reportCollection=0x%" PRIxIN ", dataIndex=%" PRIuIN ")",
          hidDevice, reportCollection, dataIndex);

    if (!hidDevice || !reportCollection) {
        return 0;
    }

    itr = reportCollection->Childs;
    while (itr != NULL) {
        switch (itr->CollectionType) {
            // Collections inside collections must be parsed recursively, so handle them
            case HID_TYPE_COLLECTION: {
                // Recursive parser for sub-collections
                calls += HidHandleReportEvent(hidDevice, (UsbHidReportCollection_t*)itr->ItemPointer, dataIndex);
            } break;

            // Input reports are interesting, that means we have an input event
            case HID_TYPE_INPUT: {
                __HandleInputItem(hidDevice, itr, dataIndex);
                calls++;
            } break;

            // For now we don't handle feature-reports
            // output reports are not handled here, but never should
            default:
                break;
        }
        itr = itr->Link;
    }
    return calls;
}

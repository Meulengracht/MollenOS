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
#include <os/keycodes.h>
#include <stdlib.h>

#include <ctt_input_service_server.h>

extern gracht_server_t* __crt_get_module_server(void);

static uint8_t __KeyboardKeyCode(uint32_t usage)
{
    if (usage >= 0x04 && usage <= 0x1D) {
        return (uint8_t)(VK_A + usage - 0x04);
    }
    if (usage >= 0x3A && usage <= 0x45) {
        return (uint8_t)(VK_F1 + usage - 0x3A);
    }
    switch (usage) {
        case 0x1E: return VK_1; case 0x1F: return VK_2; case 0x20: return VK_3;
        case 0x21: return VK_4; case 0x22: return VK_5; case 0x23: return VK_6;
        case 0x24: return VK_7; case 0x25: return VK_8; case 0x26: return VK_9;
        case 0x27: return VK_0; case 0x28: return VK_ENTER; case 0x29: return VK_ESCAPE;
        case 0x2A: return VK_BACK; case 0x2B: return VK_TAB; case 0x2C: return VK_SPACE;
        case 0x2D: return VK_HYPHEN; case 0x2E: return VK_EQUAL; case 0x2F: return VK_LBRACKET;
        case 0x30: return VK_RBRACKET; case 0x31: return VK_BACKSLASH; case 0x33: return VK_SEMICOLON;
        case 0x34: return VK_APOSTROPHE; case 0x35: return VK_BACKTICK; case 0x36: return VK_COMMA;
        case 0x37: return VK_DOT; case 0x38: return VK_SLASH; case 0x39: return VK_CAPSLOCK;
        case 0x4F: return VK_RIGHT; case 0x50: return VK_LEFT; case 0x51: return VK_DOWN;
        case 0x52: return VK_UP; case 0x53: return VK_NUMLOCK;
        case 0xE0: return VK_LCONTROL; case 0xE1: return VK_LSHIFT; case 0xE2: return VK_LALT;
        case 0xE3: return VK_LWIN; case 0xE4: return VK_RCONTROL; case 0xE5: return VK_RSHIFT;
        case 0xE6: return VK_RALT; case 0xE7: return VK_RWIN;
        default: return VK_INVALID;
    }
}

static uint16_t __KeyboardModifier(uint32_t usage)
{
    switch (usage) {
        case 0xE0: return VK_MODIFIER_LCTRL; case 0xE1: return VK_MODIFIER_LSHIFT;
        case 0xE2: return VK_MODIFIER_LALT; case 0xE4: return VK_MODIFIER_RCTRL;
        case 0xE5: return VK_MODIFIER_RSHIFT; case 0xE6: return VK_MODIFIER_RALT;
        default: return 0;
    }
}

static int64_t __SignExtend(uint64_t value, uint32_t bits)
{
    if (bits == 0 || bits >= 64) {
        return (int64_t)value;
    }
    if (value & (UINT64_C(1) << (bits - 1))) {
        value |= UINT64_MAX << bits;
    }
    return (int64_t)value;
}

/**
 * Retrieves a value from a buffer by the given bit-offset and the for a certain
 * number of bits, the extracted value will be treated unsigned
 */
static uint64_t __ExtractValue(
        _In_ const uint8_t* buffer,
        _In_ uint32_t       bitOffset,
        _In_ uint32_t       numBits,
        _In_ size_t         bufferBits)
{
    uint64_t value = 0;
    uint32_t i = 0;
    uint32_t offset = bitOffset;

    if (numBits == 0 || numBits > 64) {
        return 0;
    }
    if ((uint64_t)bitOffset + numBits > bufferBits) {
        return 0;
    }

    while (i < numBits) {
        uint32_t bits = 8 - (offset % 8);
        uint32_t remaining = numBits - i;
        uint64_t mask;
        if (bits > remaining) {
            bits = remaining;
        }
        mask = bits == 64 ? UINT64_MAX : ((UINT64_C(1) << bits) - 1);
        value |= (((uint64_t)buffer[offset / 8] >> (offset % 8)) & mask) << i;
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
            int64_t relativeValue;

            // If the value is absolute, we want to
            // make sure we calculate the relative
            if (context->InputItem->Flags == REPORT_INPUT_TYPE_ABSOLUTE) {
                // Negative logical ranges are encoded as signed values, so both
                // sides must be sign-extended before subtracting, otherwise a
                // crossing of the sign boundary produces a bogus large delta.
                if (context->CollectionItem->Stats.LogicalMin < 0) {
                    relativeValue = __SignExtend(value, numberOfValueBits) -
                                    __SignExtend(oldValue, numberOfValueBits);
                }
                else {
                    relativeValue = (int64_t)value - (int64_t)oldValue;
                }
            }
            else {
                relativeValue = __SignExtend(value, numberOfValueBits);
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

    if (dataIndex >= hidDevice->BufferSize ||
        hidDevice->PreviousDataIndex >= hidDevice->BufferSize ||
        hidDevice->Buffer == NULL ||
        hidDevice->ReportLength == 0 ||
        hidDevice->ReportLength > hidDevice->BufferSize - dataIndex) {
        WARNING("HID report does not fit in the transfer buffer");
        return;
    }

    data         = &((uint8_t*)hidDevice->Buffer)[dataIndex];
    previousData = &((uint8_t*)hidDevice->Buffer)[hidDevice->PreviousDataIndex];

    // If report-ids are active, we must make sure this data-packet
    // is actually for this report. Report ID 0 is valid, so use the
    // parser state instead of UUID_INVALID as the sentinel.
    if (collectionItem->Stats.HasReportId) {
        uint8_t reportId = data[0];
        if (reportId != (uint8_t)collectionItem->Stats.ReportId) {
            return;
        }
    }

    // Extract some of the state variables for parsing
    offset = inputItem->LocalState.BitOffset;
    if (collectionItem->Stats.HasReportId) {
        offset += 8;
    }
    length = collectionItem->Stats.ReportSize;

    // initialize context before handling
    context.CollectionItem = collectionItem;
    context.InputItem = inputItem;
    if ((uint64_t)offset + length > (uint64_t)hidDevice->ReportLength * 8ULL) {
        return;
    }

    for (i = 0; i < collectionItem->Stats.ReportCount; i++, offset += length) {
        uint64_t value    = __ExtractValue(data, offset, length, (size_t)hidDevice->ReportLength * 8);
        uint64_t oldValue = hidDevice->PreviousDataValid ?
            __ExtractValue(previousData, offset, length, (size_t)hidDevice->ReportLength * 8) : 0;

        if ((uint64_t)offset + length > (uint64_t)hidDevice->ReportLength * 8ULL) {
            break;
        }

        // We cant expect this to be correct though, it might be 0
        int usage = i < 16 ? inputItem->LocalState.Usages[i] : 0;
        if (usage == 0 && inputItem->LocalState.UsageMax >= inputItem->LocalState.UsageMin) {
            usage = (int)(inputItem->LocalState.UsageMin + i);
        }

        // Take action based on the type of input
        // currently we only handle generic pc input devices
        switch (collectionItem->Stats.UsagePage) {
            case HID_USAGE_PAGE_GENERIC_PC: {
                __HandleInputUsageGenericPc(&context, usage, oldValue, value, length);
            } break;

            // Describes keyboard or keypad events
            // See values in hid_keycodes.h
            case HID_REPORT_USAGE_PAGE_KEYBOARD: {
                if (value != oldValue) {
                        uint32_t keyUsage = inputItem->Flags == REPORT_INPUT_TYPE_ARRAY ?
                            (uint32_t)(value != 0 ? value : oldValue) : (uint32_t)usage;
                    uint8_t keycode = __KeyboardKeyCode(keyUsage);
                    uint16_t modifiers = __KeyboardModifier(keyUsage);
                    if (keycode != VK_INVALID) {
                        if (value == 0) {
                            modifiers |= VK_MODIFIER_RELEASED;
                        }
                        ctt_input_event_button_event_all(__crt_get_module_server(),
                                hidDevice->Base->Base.Id, keycode, modifiers);
                    }
                }
            } break;

            // Generic button event (Mouse)
            // Possible values go through 1..65535 (determined by logical min/max)
            case HID_REPORT_USAGE_PAGE_BUTTON: {
                // Check against old values if any changes are neccessary
                if (value == oldValue) {
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
                        if (usage < 1 || usage > 8) {
                            break;
                        }
                        uint8_t button;
                        if (usage == 1) {
                            button = VK_LBUTTON;
                        } else if (usage == 2) {
                            button = VK_RBUTTON;
                        } else if (usage == 3) {
                            button = VK_MBUTTON;
                        } else {
                            button = (uint8_t)(VK_RBUTTON + usage - 2);
                        }
                        uint16_t modifiers = value ? 0 : VK_MODIFIER_RELEASED;
                        ctt_input_event_button_event_all(__crt_get_module_server(),
                            hidDevice->Base->Base.Id, button, modifiers);
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
    if (collectionItem->InputType == CTT_INPUT_TYPE_MOUSE &&
        (context.EventData.PointerEvent.rel_x || context.EventData.PointerEvent.rel_y ||
         context.EventData.PointerEvent.rel_z)) {
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

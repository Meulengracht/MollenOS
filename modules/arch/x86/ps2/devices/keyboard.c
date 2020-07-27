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
 * along with this program.If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * X86 PS2 Controller (Mouse) Driver
 * http://wiki.osdev.org/PS2
 */
//#define __TRACE

#include <ddk/utils.h>
#include <errno.h>
#include <event.h>
#include <gracht/link/vali.h>
#include <io.h>
#include <os/keycodes.h>
#include "keyboard.h"
#include "../ps2.h"
#include <string.h>
#include <stdlib.h>

OsStatus_t
PS2KeyboardHandleModifiers(
    _In_ PS2Port_t*                     Port,
    _In_ struct ctt_input_button_event* Key)
{
    uint16_t Flags = *((uint16_t*)&PS2_KEYBOARD_DATA_STATE_LO(Port));

    // Handle modifiers
    switch (Key->key_code) {
        case VK_LSHIFT: {
            Flags |= VK_MODIFIER_LSHIFT;
            if (Key->modifiers & VK_MODIFIER_RELEASED) {
                Flags &= ~(VK_MODIFIER_LSHIFT);
            }
        } break;
        case VK_RSHIFT: {
            Flags |= VK_MODIFIER_RSHIFT;
            if (Key->modifiers & VK_MODIFIER_RELEASED) {
                Flags &= ~(VK_MODIFIER_RSHIFT);
            }
        } break;
        
        case VK_LCONTROL: {
            Flags |= VK_MODIFIER_LCTRL;
            if (Key->modifiers & VK_MODIFIER_RELEASED) {
                Flags &= ~(VK_MODIFIER_LCTRL);
            }
        } break;
        case VK_RCONTROL: {
            Flags |= VK_MODIFIER_RCTRL;
            if (Key->modifiers & VK_MODIFIER_RELEASED) {
                Flags &= ~(VK_MODIFIER_RCTRL);
            }
        } break;
        
        case VK_LALT: {
            Flags |= VK_MODIFIER_LALT;
            if (Key->modifiers & VK_MODIFIER_RELEASED) {
                Flags &= ~(VK_MODIFIER_LALT);
            }
        } break;
        case VK_RALT: {
            Flags |= VK_MODIFIER_RALT;
            if (Key->modifiers & VK_MODIFIER_RELEASED) {
                Flags &= ~(VK_MODIFIER_RALT);
            }
        } break;

        case VK_SCROLL: {
            Flags |= VK_MODIFIER_SCROLLOCK;
            if (Key->modifiers & VK_MODIFIER_RELEASED) {
                Flags &= ~(VK_MODIFIER_SCROLLOCK);
            }
        } break;
        case VK_NUMLOCK: {
            Flags |= VK_MODIFIER_NUMLOCK;
            if (Key->modifiers & VK_MODIFIER_RELEASED) {
                Flags &= ~(VK_MODIFIER_NUMLOCK);
            }
        } break;
        case VK_CAPSLOCK: {
            Flags |= VK_MODIFIER_CAPSLOCK;
            if (Key->modifiers & VK_MODIFIER_RELEASED) {
                Flags &= ~(VK_MODIFIER_CAPSLOCK);
            }
        } break;

        default: {
            Key->modifiers |= Flags;
            return OsSuccess;
        };
    }

    // Update the state flags
    Key->modifiers |= Flags;
    *((uint16_t*)&PS2_KEYBOARD_DATA_STATE_LO(Port)) = Flags;
    return OsError;
}

InterruptStatus_t
PS2KeyboardFastInterrupt(
        _In_ InterruptFunctionTable_t* InterruptTable,
        _In_ InterruptResourceTable_t* ResourceTable)
{
    DeviceIo_t*   IoSpace      = INTERRUPT_IOSPACE(ResourceTable, 0);
    PS2Port_t*    Port         = (PS2Port_t*)INTERRUPT_RESOURCE(ResourceTable, 0);
    uint8_t       DataRecieved = (uint8_t)InterruptTable->ReadIoSpace(IoSpace, PS2_REGISTER_DATA, 1);
    PS2Command_t* Command      = &Port->ActiveCommand;

    if (Command->State != PS2Free) {
        Command->Buffer[Command->SyncObject] = DataRecieved;
        Command->SyncObject++;
    }
    else {
        Port->ResponseBuffer[Port->ResponseWriteIndex++] = DataRecieved;
        if (Port->ResponseWriteIndex == PS2_RINGBUFFER_SIZE) {
            Port->ResponseWriteIndex = 0; // Start over
        }

        // Determine if it is an actual scancode or extension code
        if (DataRecieved != PS2_CODE_EXTENDED && DataRecieved != PS2_CODE_RELEASED) {
            InterruptTable->EventSignal(ResourceTable->ResourceHandle);
        }
    }
    return InterruptHandled;
}

void
PS2KeyboardInterrupt(
    _In_ PS2Port_t* Port)
{
    struct ctt_input_button_event Key         = { 0 };
    OsStatus_t                    Status      = OsError;
    uint8_t                       ScancodeSet = PS2_KEYBOARD_DATA_SCANCODESET(Port);

    // Perform scancode-translation
    while (Status == OsError) {
        uint8_t Scancode = Port->ResponseBuffer[Port->ResponseReadIndex++];
        if (Port->ResponseReadIndex == PS2_RINGBUFFER_SIZE) {
            Port->ResponseReadIndex = 0; // Start over
        }

        if (ScancodeSet == 1) {
            ERROR("PS2-Keyboard: Scancode set 1");
            break;
        }
        else if (ScancodeSet == 2) {
            Status = ScancodeSet2ToVKey(&Key, Scancode);
        }
    }

    // If the key was an actual key and not modifier, remove our flags and send
    if (PS2KeyboardHandleModifiers(Port, &Key) == OsSuccess) {
        Key.modifiers &= ~(KEY_MODIFIER_EXTENDED);
        ctt_input_event_button_all(Port->DeviceId, Key.key_code, Key.modifiers, Key.key_ascii, Key.key_unicode);
    }
}

OsStatus_t
PS2KeyboardGetScancode(
    _In_  PS2Port_t* Port,
    _Out_ uint8_t*   ResultSet)
{
    uint8_t Response = 0;
    if (PS2PortExecuteCommand(Port, PS2_KEYBOARD_SCANCODE, &Response) != OsSuccess) {
        return OsError;
    }

    // Bit 6 is set in case its translation
    *ResultSet = Response & 0xF;
    if (Response >= 0xF0) {
        return OsError;
    }
    else {
        return OsSuccess;
    }
}

OsStatus_t PS2KeyboardSetScancode(
    _In_ PS2Port_t* Port,
    _In_ uint8_t    RequestSet)
{
    if (PS2PortExecuteCommand(Port, PS2_KEYBOARD_SCANCODE, NULL) != OsSuccess || 
        PS2PortExecuteCommand(Port, RequestSet, NULL)            != OsSuccess) {
        return OsError;
    }
    else {
        PS2_KEYBOARD_DATA_SCANCODESET(Port) = RequestSet;
        return OsSuccess;
    }
}

OsStatus_t
PS2KeyboardSetTypematics(
    _In_ PS2Port_t* Port)
{
    uint8_t Format = 0;

    // Build the data-packet
    Format |= PS2_KEYBOARD_DATA_REPEAT(Port);
    Format |= PS2_KEYBOARD_DATA_DELAY(Port);

    // Write the command to get scancode set
    if (PS2PortExecuteCommand(Port, PS2_KEYBOARD_TYPEMATIC, NULL) != OsSuccess || 
        PS2PortExecuteCommand(Port, Format, NULL)                 != OsSuccess) {
        return OsError;
    }
    else {
        return OsSuccess;
    }
}

OsStatus_t
PS2KeyboardSetLEDs(
    _In_ PS2Port_t* Port,
    _In_ int        Scroll,
    _In_ int        Number,
    _In_ int        Caps)
{
    uint8_t Format = 0;

    // Build the data-packet
    Format |= ((uint8_t)(Scroll & 0x1) << 0);
    Format |= ((uint8_t)(Number & 0x1) << 1);
    Format |= ((uint8_t)(Caps & 0x1) << 2);

    // Write the command to get scancode set
    if (PS2PortExecuteCommand(Port, PS2_KEYBOARD_SETLEDS, NULL)   != OsSuccess || 
        PS2PortExecuteCommand(Port, Format, NULL)                 != OsSuccess) {
        return OsError;
    }
    else {
        return OsSuccess;
    }
}

OsStatus_t
PS2KeyboardInitialize(
    _In_ PS2Controller_t* Controller,
    _In_ int              port,
    _In_ int              Translation)
{
    gracht_client_configuration_t      client_config;
    struct socket_client_configuration link_config;
    PS2Port_t*                         instance = &Controller->Ports[port];
    int                                status;
    TRACE("... [ps2] [keyboard] initialize");

    // Initialize keyboard defaults
    PS2_KEYBOARD_DATA_XLATION(instance)     = (uint8_t)Translation;
    PS2_KEYBOARD_DATA_SCANCODESET(instance) = 2;
    PS2_KEYBOARD_DATA_REPEAT(instance)      = PS2_REPEATS_PERSEC(16);
    PS2_KEYBOARD_DATA_DELAY(instance)       = PS2_DELAY_500MS;

    // Open up the input socket so we can send input data to the OS.
    link_config.type = gracht_link_packet_based;
    gracht_os_get_server_packet_address(&link_config.address, &link_config.address_length);
    
    status = gracht_link_socket_client_create(&client_config.link, &link_config);
    if (status) {
        ERROR("... [ps2] [keyboard] [initialize] gracht_link_socket_client_create failed %i", errno);
    }
    
    if (status && gracht_client_create(&client_config, &instance->GrachtClient)) {
        ERROR("... [ps2] [keyboard] [initialize] gracht_client_create failed %i", errno);
    }

    // Initialize interrupt
    RegisterFastInterruptIoResource(&instance->Interrupt, Controller->Data);
    RegisterFastInterruptHandler(&instance->Interrupt, (InterruptHandler_t)PS2KeyboardFastInterrupt);
    instance->InterruptId = RegisterInterruptSource(&instance->Interrupt, INTERRUPT_EXCLUSIVE);

    // Reset keyboard LEDs status
    if (PS2KeyboardSetLEDs(instance, 0, 0, 0) != OsSuccess) {
        ERROR("PS2-Keyboard: failed to reset LEDs");
    }

    // Update typematics to preffered settings
    if (PS2KeyboardSetTypematics(instance) != OsSuccess) {
        WARNING("PS2-Keyboard: failed to set typematic settings");
    }
    
    // Select our preffered scancode set
    if (PS2KeyboardSetScancode(instance, 2) != OsSuccess) {
        WARNING("PS2-Keyboard: failed to select scancodeset 2 (%u)", 
            PS2_KEYBOARD_DATA_SCANCODESET(instance));
    }

    instance->State = PortStateActive;
    return PS2PortExecuteCommand(instance, PS2_ENABLE_SCANNING, NULL);
}

OsStatus_t
PS2KeyboardCleanup(
    _In_ PS2Controller_t* controller,
    _In_ int              index)
{
    PS2Port_t* port = &controller->Ports[index];

    // Try to disable the device before cleaning up
    PS2PortExecuteCommand(port, PS2_DISABLE_SCANNING, NULL);
    UnregisterInterruptSource(port->InterruptId);
    
    gracht_client_shutdown(port->GrachtClient);
    port->Signature = 0xFFFFFFFF;
    port->State     = PortStateConnected;
    return OsSuccess;
}

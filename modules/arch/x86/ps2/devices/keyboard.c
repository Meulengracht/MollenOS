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
    _In_ PS2Port_t*                     port,
    _In_ struct ctt_input_button_event* buttonEvent)
{
    uint16_t Flags = *((uint16_t*)&PS2_KEYBOARD_DATA_STATE_LO(port));

    // Handle modifiers
    switch (buttonEvent->key_code) {
        case VK_LSHIFT: {
            Flags |= VK_MODIFIER_LSHIFT;
            if (buttonEvent->modifiers & VK_MODIFIER_RELEASED) {
                Flags &= ~(VK_MODIFIER_LSHIFT);
            }
        } break;
        case VK_RSHIFT: {
            Flags |= VK_MODIFIER_RSHIFT;
            if (buttonEvent->modifiers & VK_MODIFIER_RELEASED) {
                Flags &= ~(VK_MODIFIER_RSHIFT);
            }
        } break;
        
        case VK_LCONTROL: {
            Flags |= VK_MODIFIER_LCTRL;
            if (buttonEvent->modifiers & VK_MODIFIER_RELEASED) {
                Flags &= ~(VK_MODIFIER_LCTRL);
            }
        } break;
        case VK_RCONTROL: {
            Flags |= VK_MODIFIER_RCTRL;
            if (buttonEvent->modifiers & VK_MODIFIER_RELEASED) {
                Flags &= ~(VK_MODIFIER_RCTRL);
            }
        } break;
        
        case VK_LALT: {
            Flags |= VK_MODIFIER_LALT;
            if (buttonEvent->modifiers & VK_MODIFIER_RELEASED) {
                Flags &= ~(VK_MODIFIER_LALT);
            }
        } break;
        case VK_RALT: {
            Flags |= VK_MODIFIER_RALT;
            if (buttonEvent->modifiers & VK_MODIFIER_RELEASED) {
                Flags &= ~(VK_MODIFIER_RALT);
            }
        } break;

        case VK_SCROLL: {
            Flags |= VK_MODIFIER_SCROLLOCK;
            if (buttonEvent->modifiers & VK_MODIFIER_RELEASED) {
                Flags &= ~(VK_MODIFIER_SCROLLOCK);
            }
        } break;
        case VK_NUMLOCK: {
            Flags |= VK_MODIFIER_NUMLOCK;
            if (buttonEvent->modifiers & VK_MODIFIER_RELEASED) {
                Flags &= ~(VK_MODIFIER_NUMLOCK);
            }
        } break;
        case VK_CAPSLOCK: {
            Flags |= VK_MODIFIER_CAPSLOCK;
            if (buttonEvent->modifiers & VK_MODIFIER_RELEASED) {
                Flags &= ~(VK_MODIFIER_CAPSLOCK);
            }
        } break;

        default: {
            buttonEvent->modifiers |= Flags;
            return OsSuccess;
        };
    }

    // Update the state flags
    buttonEvent->modifiers |= Flags;
    *((uint16_t*)&PS2_KEYBOARD_DATA_STATE_LO(port)) = Flags;
    return OsError;
}

InterruptStatus_t
PS2KeyboardFastInterrupt(
        _In_ InterruptFunctionTable_t* interruptTable,
        _In_ InterruptResourceTable_t* resourceTable)
{
    DeviceIo_t*   ioSpace      = INTERRUPT_IOSPACE(resourceTable, 0);
    PS2Port_t*    port         = (PS2Port_t*)INTERRUPT_RESOURCE(resourceTable, 0);
    uint8_t       dataRecieved = (uint8_t)interruptTable->ReadIoSpace(ioSpace, PS2_REGISTER_DATA, 1);
    PS2Command_t* command      = &port->ActiveCommand;

    if (command->State != PS2Free) {
        command->Buffer[command->SyncObject] = dataRecieved;
        smp_wmb();
        command->SyncObject++;
    }
    else {
        port->ResponseBuffer[port->ResponseWriteIndex % PS2_RINGBUFFER_SIZE] = dataRecieved;
        smp_wmb();
        port->ResponseWriteIndex++;

        // Determine if it is an actual scancode or extension code
        if (dataRecieved != PS2_CODE_EXTENDED && dataRecieved != PS2_CODE_RELEASED) {
            interruptTable->EventSignal(resourceTable->HandleResource);
        }
    }

    return InterruptHandled;
}

void
PS2KeyboardInterrupt(
    _In_ PS2Port_t* port)
{
    struct ctt_input_button_event buttonEvent;
    OsStatus_t                    status;
    uint8_t                       scancode;
    uint8_t                       scancodeSet = PS2_KEYBOARD_DATA_SCANCODESET(port);
    uint32_t                      readIndex   = port->ResponseReadIndex;

    smp_rmb(); // wait for all loads to be done before reading write_index
    while (readIndex < port->ResponseWriteIndex) {
        memset(&buttonEvent, 0, sizeof(struct ctt_input_button_event));
        status = OsError;

        // Perform scancode-translation
        while (status == OsError && readIndex < port->ResponseWriteIndex) {
            scancode = port->ResponseBuffer[readIndex % PS2_RINGBUFFER_SIZE];
            if (scancodeSet == 1) {
                ERROR("PS2-Keyboard: Scancode set 1");
                break;
            }
            else if (scancodeSet == 2) {
                status = ScancodeSet2ToVKey(&buttonEvent, scancode);
            }
            readIndex++;
        }

        if (status == OsSuccess) {
            port->ResponseReadIndex = readIndex;

            // If the key was an actual key and not modifier, remove our flags and send
            if (PS2KeyboardHandleModifiers(port, &buttonEvent) == OsSuccess) {
                buttonEvent.modifiers &= ~(KEY_MODIFIER_EXTENDED);
                ctt_input_event_button_all(port->DeviceId, buttonEvent.key_code, buttonEvent.modifiers);
            }
        }
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

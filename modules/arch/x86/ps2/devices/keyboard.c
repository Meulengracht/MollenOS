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

extern gracht_server_t* __crt_get_module_server(void);

#define IS_SWITCH(keyCode) ((keyCode) == VK_CAPSLOCK || (keyCode) == VK_SCROLL || (keyCode) == VK_NUMLOCK)

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

static inline int __IsKeySet(
        _In_ PS2Port_t* port,
        _In_ uint8_t    keyCode)
{
    int keyBlock = keyCode / sizeof(uint8_t);
    int keyIndex = keyCode % sizeof(uint8_t);
    return port->device_data.keyboard.key_map[keyBlock] & (1 << keyIndex);
}

static inline void __SetKey(
        _In_ PS2Port_t* port,
        _In_ uint8_t    keyCode)
{
    int keyBlock = keyCode / sizeof(uint8_t);
    int keyIndex = keyCode % sizeof(uint8_t);
    port->device_data.keyboard.key_map[keyBlock] |= (1 << keyIndex);
}

static inline void __ClearKey(
        _In_ PS2Port_t* port,
        _In_ uint8_t    keyCode)
{
    int keyBlock = keyCode / sizeof(uint8_t);
    int keyIndex = keyCode % sizeof(uint8_t);
    port->device_data.keyboard.key_map[keyBlock] &= ~(1 << keyIndex);
}

static inline uint16_t __GetModifierFlag(
        _In_ uint8_t keyCode)
{
    switch (keyCode) {
        case VK_LCONTROL: return VK_MODIFIER_LCTRL;
        case VK_RCONTROL: return VK_MODIFIER_RCTRL;
        case VK_LALT:     return VK_MODIFIER_LALT;
        case VK_RALT:     return VK_MODIFIER_RALT;
        case VK_LSHIFT:   return VK_MODIFIER_LSHIFT;
        case VK_RSHIFT:   return VK_MODIFIER_RSHIFT;
        case VK_SCROLL:   return VK_MODIFIER_SCROLLOCK;
        case VK_NUMLOCK:  return VK_MODIFIER_NUMLOCK;
        case VK_CAPSLOCK: return VK_MODIFIER_CAPSLOCK;
        default: return 0;
    }
}

static void __HandleKeyCode(
    _In_ PS2Port_t*        port,
    _In_ struct key_state* buttonEvent)
{
    uint16_t modifiers = 0;
    uint16_t flag = __GetModifierFlag(buttonEvent->keycode);

    // set key pressed if pressed
    if (buttonEvent->modifiers & VK_MODIFIER_RELEASED) {
        // release event
        if (IS_SWITCH(buttonEvent->keycode)) {
            // Only clear it if it has been cleared with the second press
            if (!(port->device_data.keyboard.modifiers & flag)) {
                __ClearKey(port, buttonEvent->keycode);
            }
        }
        else {
            __ClearKey(port, buttonEvent->keycode);
            if (flag) {
                port->device_data.keyboard.modifiers &= ~(flag);
            }
        }
        modifiers |= port->device_data.keyboard.modifiers;
    }
    else {
        // press event - we handle lock-keys a bit different as they are switches
        if (IS_SWITCH(buttonEvent->keycode)) {
            port->device_data.keyboard.modifiers ^= flag;
            if (port->device_data.keyboard.modifiers & flag) {
                __SetKey(port, buttonEvent->keycode);
            }
        }
        else {
            if (__IsKeySet(port, buttonEvent->keycode)) {
                modifiers |= VK_MODIFIER_REPEATED;
            }
            else {
                __SetKey(port, buttonEvent->keycode);
                if (flag) {
                    port->device_data.keyboard.modifiers |= flag;
                }
            }
        }
        modifiers |= port->device_data.keyboard.modifiers;
    }

    // Update the state flags
    buttonEvent->modifiers |= modifiers;
}

static inline OsStatus_t __ParseKeyCode(
        _In_ PS2Port_t*        port,
        _In_ uint8_t           scancodeSet,
        _In_ uint32_t*         readIndexReference,
        _In_ struct key_state* event)
{
    OsStatus_t osStatus  = OsError;
    uint32_t   readIndex = *readIndexReference;

    while (osStatus == OsError && readIndex < port->ResponseWriteIndex) {
        uint8_t scancode = port->ResponseBuffer[readIndex % PS2_RINGBUFFER_SIZE];
        if (scancodeSet == 1) {
            ERROR("PS2-Keyboard: Scancode set 1");
            break;
        }
        else if (scancodeSet == 2) {
            osStatus = ScancodeSet2ToVKey(event, scancode);
        }
        readIndex++;
    }

    *readIndexReference = readIndex;
    return osStatus;
}

void
PS2KeyboardInterrupt(
    _In_ PS2Port_t* port)
{
    OsStatus_t status;
    uint8_t    scancodeSet = port->device_data.keyboard.scancode_set;
    uint32_t   readIndex   = port->ResponseReadIndex;

    smp_rmb(); // wait for all loads to be done before reading write_index
    while (readIndex < port->ResponseWriteIndex) {
        struct key_state buttonEvent = { 0 };

        status = __ParseKeyCode(port, scancodeSet, &readIndex, &buttonEvent);
        if (status == OsOK) {
            port->ResponseReadIndex = readIndex;

            // we do not handle invalid key-codes
            if (buttonEvent.keycode == VK_INVALID) {
                continue;
            }

            buttonEvent.modifiers &= ~(KEY_MODIFIER_EXTENDED);

            __HandleKeyCode(port, &buttonEvent);
            ctt_input_event_button_event_all(__crt_get_module_server(), port->DeviceId,
                                             buttonEvent.keycode, buttonEvent.modifiers);
        }
    }
}

OsStatus_t
PS2KeyboardGetScancode(
    _In_  PS2Port_t* Port,
    _Out_ uint8_t*   ResultSet)
{
    uint8_t Response = 0;
    if (PS2PortExecuteCommand(Port, PS2_KEYBOARD_SCANCODE, &Response) != OsOK) {
        return OsError;
    }

    // Bit 6 is set in case its translation
    *ResultSet = Response & 0xF;
    if (Response >= 0xF0) {
        return OsError;
    }
    else {
        return OsOK;
    }
}

OsStatus_t PS2KeyboardSetScancode(
    _In_ PS2Port_t* port,
    _In_ uint8_t    requestSet)
{
    if (PS2PortExecuteCommand(port, PS2_KEYBOARD_SCANCODE, NULL) != OsOK ||
        PS2PortExecuteCommand(port, requestSet, NULL) != OsOK) {
        return OsError;
    }
    else {
        port->device_data.keyboard.scancode_set = requestSet;
        return OsOK;
    }
}

OsStatus_t
PS2KeyboardSetTypematics(
    _In_ PS2Port_t* port)
{
    uint8_t data = 0;

    // Build the data-packet
    data |= port->device_data.keyboard.repeat;
    data |= port->device_data.keyboard.delay;

    // Write the command to get scancode set
    if (PS2PortExecuteCommand(port, PS2_KEYBOARD_TYPEMATIC, NULL) != OsOK ||
        PS2PortExecuteCommand(port, data, NULL) != OsOK) {
        return OsError;
    }
    else {
        return OsOK;
    }
}

OsStatus_t
PS2KeyboardSetLEDs(
    _In_ PS2Port_t* port,
    _In_ int        setScroll,
    _In_ int        setNumber,
    _In_ int        setCaps)
{
    uint8_t data = 0;

    // Build the data-packet
    data |= ((uint8_t)(setScroll & 0x1) << 0);
    data |= ((uint8_t)(setNumber & 0x1) << 1);
    data |= ((uint8_t)(setCaps & 0x1) << 2);

    // Write the command to get scancode set
    if (PS2PortExecuteCommand(port, PS2_KEYBOARD_SETLEDS, NULL) != OsOK ||
        PS2PortExecuteCommand(port, data, NULL) != OsOK) {
        return OsError;
    }
    else {
        return OsOK;
    }
}

OsStatus_t
PS2KeyboardInitialize(
    _In_ PS2Controller_t* controller,
    _In_ int              portIndex,
    _In_ int              translation)
{
    PS2Port_t* port = &controller->Ports[portIndex];
    TRACE("PS2KeyboardInitialize(portIndex=%i, translation=%i)", portIndex, translation);

    // Initialize keyboard defaults
    memset(&port->device_data, 0, sizeof(port->device_data));
    port->device_data.keyboard.xlation      = (uint8_t)translation;
    port->device_data.keyboard.scancode_set = 2;
    port->device_data.keyboard.repeat       = PS2_REPEATS_PERSEC(16);
    port->device_data.keyboard.delay        = PS2_DELAY_500MS;

    // Initialize interrupt
    RegisterFastInterruptIoResource(&port->Interrupt, controller->Data);
    RegisterFastInterruptHandler(&port->Interrupt, (InterruptHandler_t)PS2KeyboardFastInterrupt);
    port->InterruptId = RegisterInterruptSource(&port->Interrupt, INTERRUPT_EXCLUSIVE);

    // Reset keyboard LEDs status
    if (PS2KeyboardSetLEDs(port, 0, 0, 0) != OsOK) {
        ERROR("PS2KeyboardInitialize: failed to reset LEDs");
    }

    // Update typematics to preffered settings
    if (PS2KeyboardSetTypematics(port) != OsOK) {
        WARNING("PS2KeyboardInitialize: failed to set typematic settings");
    }
    
    // Select our preffered scancode set
    if (PS2KeyboardSetScancode(port, 2) != OsOK) {
        WARNING("PS2KeyboardInitialize: failed to select scancodeset 2 (%u)",
                port->device_data.keyboard.scancode_set);
    }

    port->State = PortStateActive;
    return PS2PortExecuteCommand(port, PS2_ENABLE_SCANNING, NULL);
}

OsStatus_t
PS2KeyboardCleanup(
    _In_ PS2Controller_t* controller,
    _In_ int              portIndex)
{
    PS2Port_t* port = &controller->Ports[portIndex];

    // Try to disable the device before cleaning up
    PS2PortExecuteCommand(port, PS2_DISABLE_SCANNING, NULL);
    UnregisterInterruptSource(port->InterruptId);

    port->Signature = 0xFFFFFFFF;
    port->State     = PortStateConnected;
    return OsOK;
}

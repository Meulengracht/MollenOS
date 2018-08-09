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
 * MollenOS X86 PS2 Controller (Mouse) Driver
 * http://wiki.osdev.org/PS2
 */
//#define __TRACE

#include <os/input.h>
#include <os/utils.h>
#include <string.h>
#include <stdlib.h>
#include "keyboard.h"
#include "../ps2.h"

// Scancode sets
OsStatus_t ScancodeSet2ToVKey(SystemKey_t* KeyState, uint8_t Scancode);

/* PS2KeyboardHandleModifiers
 * Handles special modifiers that should be applied instead of notified. */
OsStatus_t
PS2KeyboardHandleModifiers(
    _In_ PS2Port_t*                 Port,
    _In_ SystemKey_t*               Key)
{
    uint16_t Flags  = (uint16_t)PS2_KEYBOARD_DATA_STATE_HI(Port) << 8;
    Flags          |= PS2_KEYBOARD_DATA_STATE_LO(Port);

    // Handle modifiers
    switch (Key->KeyCode) {
        case VK_LSHIFT: {
            Flags |= KEY_MODIFIER_LSHIFT;
            if (Key->Flags & KEY_MODIFIER_RELEASED) {
                Flags &= ~(KEY_MODIFIER_LSHIFT);
            }
        } break;
        case VK_RSHIFT: {
            Flags |= KEY_MODIFIER_RSHIFT;
            if (Key->Flags & KEY_MODIFIER_RELEASED) {
                Flags &= ~(KEY_MODIFIER_RSHIFT);
            }
        } break;
        
        case VK_LCONTROL: {
            Flags |= KEY_MODIFIER_LCTRL;
            if (Key->Flags & KEY_MODIFIER_RELEASED) {
                Flags &= ~(KEY_MODIFIER_LCTRL);
            }
        } break;
        case VK_RCONTROL: {
            Flags |= KEY_MODIFIER_RCTRL;
            if (Key->Flags & KEY_MODIFIER_RELEASED) {
                Flags &= ~(KEY_MODIFIER_RCTRL);
            }
        } break;
        
        case VK_LALT: {
            Flags |= KEY_MODIFIER_LALT;
            if (Key->Flags & KEY_MODIFIER_RELEASED) {
                Flags &= ~(KEY_MODIFIER_LALT);
            }
        } break;
        case VK_RALT: {
            Flags |= KEY_MODIFIER_RALT;
            if (Key->Flags & KEY_MODIFIER_RELEASED) {
                Flags &= ~(KEY_MODIFIER_RALT);
            }
        } break;

        case VK_SCROLL: {
            Flags |= KEY_MODIFIER_SCROLLLOCK;
            if (Key->Flags & KEY_MODIFIER_RELEASED) {
                Flags &= ~(KEY_MODIFIER_SCROLLLOCK);
            }
        } break;
        case VK_NUMLOCK: {
            Flags |= KEY_MODIFIER_NUMLOCK;
            if (Key->Flags & KEY_MODIFIER_RELEASED) {
                Flags &= ~(KEY_MODIFIER_NUMLOCK);
            }
        } break;
        case VK_CAPSLOCK: {
            Flags |= KEY_MODIFIER_CAPSLOCK;
            if (Key->Flags & KEY_MODIFIER_RELEASED) {
                Flags &= ~(KEY_MODIFIER_CAPSLOCK);
            }
        } break;

        default:
            return OsSuccess;
    }

    // Update the state flags
    Key->Flags                      |= Flags;
    PS2_KEYBOARD_DATA_STATE_LO(Port) = LOBYTE(Flags);
    PS2_KEYBOARD_DATA_STATE_HI(Port) = HIBYTE(Flags);
    return OsError;
}

/* PS2KeyboardFastInterrupt 
 * Handles the ps2-keyboard interrupt and extracts the data for processing - fast interrupt */
InterruptStatus_t
PS2KeyboardFastInterrupt(
    _In_ FastInterruptResources_t*  InterruptTable,
    _In_ void*                      Unused)
{
    DeviceIo_t* IoSpace     = INTERRUPT_IOSPACE(InterruptTable, 0);
    PS2Port_t* Port         = (PS2Port_t*)INTERRUPT_RESOURCE(InterruptTable, 0);
    uint8_t DataRecieved    = (uint8_t)InterruptTable->ReadIoSpace(IoSpace, PS2_REGISTER_DATA, 1);
    PS2Command_t* Command   = &Port->ActiveCommand;

    if (Command->State != PS2Free) {
        Command->Buffer[Command->SyncObject] = DataRecieved;
        Command->SyncObject++;
        return InterruptHandledStop;
    }
    else {
        Port->ResponseBuffer[Port->ResponseWriteIndex++] = DataRecieved;
        if (Port->ResponseWriteIndex == PS2_RINGBUFFER_SIZE) {
            Port->ResponseWriteIndex = 0; // Start over
        }

        // Determine if it is an actual scancode or extension code
        if (DataRecieved != PS2_CODE_EXTENDED && DataRecieved != PS2_CODE_RELEASED) {
            return InterruptHandled;
        }
    }
    return InterruptHandledStop;
}

/* PS2KeyboardInterrupt 
 * Handles the ps2-keyboard interrupt and processes the captured data */
InterruptStatus_t
PS2KeyboardInterrupt(
    _In_ PS2Port_t*                 Port)
{
    OsStatus_t Status   = OsError;
    uint8_t ScancodeSet = PS2_KEYBOARD_DATA_SCANCODESET(Port);
    SystemKey_t Key;

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
        Key.Flags  &= ~(KEY_MODIFIER_EXTENDED);
        Status      = WriteSystemKey(&Key);
    }
    return InterruptHandled;
}

/* PS2KeyboardGetScancode
 * Retrieves the current scancode-set for the keyboard */
OsStatus_t
PS2KeyboardGetScancode(
    _In_  PS2Port_t*                Port,
    _Out_ uint8_t*                  ResultSet)
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

/* PS2KeyboardSetScancode
 * Updates the current scancode-set for the keyboard */
OsStatus_t PS2KeyboardSetScancode(
    _In_ PS2Port_t*                 Port,
    _In_ uint8_t                    RequestSet)
{
    if (PS2PortExecuteCommand(Port, PS2_KEYBOARD_SCANCODE, NULL)  != OsSuccess || 
        PS2PortExecuteCommand(Port, RequestSet, NULL)             != OsSuccess) {
        return OsError;
    }
    else {
        PS2_KEYBOARD_DATA_SCANCODESET(Port) = RequestSet;
        return OsSuccess;
    }
}

/* PS2KeyboardSetTypematics
 * Updates the current typematics for the keyboard */
OsStatus_t
PS2KeyboardSetTypematics(
    _In_ PS2Port_t*                 Port)
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

/* PS2KeyboardSetLEDs
 * Updates the LED statuses for the ps2 keyboard */
OsStatus_t
PS2KeyboardSetLEDs(
    _In_ PS2Port_t*                 Port,
    _In_ int                        Scroll,
    _In_ int                        Number,
    _In_ int                        Caps)
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

/* PS2KeyboardInitialize 
 * Initializes an instance of an ps2-keyboard on the given PS2-Controller port */
OsStatus_t
PS2KeyboardInitialize(
    _In_ PS2Controller_t*           Controller,
    _In_ int                        Port,
    _In_ int                        Translation)
{
    PS2Port_t *Instance = &Controller->Ports[Port];

    // Initialize keyboard defaults
    PS2_KEYBOARD_DATA_XLATION(Instance)     = (uint8_t)Translation;
    PS2_KEYBOARD_DATA_SCANCODESET(Instance) = 2;
    PS2_KEYBOARD_DATA_REPEAT(Instance)      = PS2_REPEATS_PERSEC(16);
    PS2_KEYBOARD_DATA_DELAY(Instance)       = PS2_DELAY_500MS;

    // Start out by initializing the contract
    InitializeContract(&Instance->Contract, Instance->Contract.DeviceId, 1,
        ContractInput, "PS2 Keyboard Interface");

    // Register our contract for this device
    if (RegisterContract(&Instance->Contract) != OsSuccess) {
        ERROR("PS2-Keyboard: failed to install contract");
        return OsError;
    }
    
    // Initialize interrupt
    RegisterFastInterruptIoResource(&Instance->Interrupt, Controller->Data);
    RegisterFastInterruptHandler(&Instance->Interrupt, PS2KeyboardFastInterrupt);
    Instance->InterruptId = RegisterInterruptSource(&Instance->Interrupt, INTERRUPT_USERSPACE | INTERRUPT_NOTSHARABLE);

    // Reset keyboard LEDs status
    if (PS2KeyboardSetLEDs(Instance, 0, 0, 0) != OsSuccess) {
        ERROR("PS2-Keyboard: failed to reset LEDs");
    }

    // Update typematics to preffered settings
    if (PS2KeyboardSetTypematics(Instance) != OsSuccess) {
        WARNING("PS2-Keyboard: failed to set typematic settings");
    }
    
    // Select our preffered scancode set
    if (PS2KeyboardSetScancode(Instance, 2) != OsSuccess) {
        WARNING("PS2-Keyboard: failed to select scancodeset 2 (%u)", 
            PS2_KEYBOARD_DATA_SCANCODESET(Instance));
    }

    Instance->State = PortStateActive;
    return PS2PortExecuteCommand(Instance, PS2_ENABLE_SCANNING, NULL);
}

/* PS2KeyboardCleanup 
 * Cleans up the ps2-keyboard instance on the given PS2-Controller port */
OsStatus_t
PS2KeyboardCleanup(
    _In_ PS2Controller_t*           Controller,
    _In_ int                        Port)
{
    PS2Port_t *Instance = &Controller->Ports[Port];

    // Try to disable the device before cleaning up
    PS2PortExecuteCommand(Instance, PS2_DISABLE_SCANNING, NULL);
    UnregisterInterruptSource(Instance->InterruptId);

    Instance->Signature = 0xFFFFFFFF;
    Instance->State     = PortStateConnected;
    return OsSuccess;
}

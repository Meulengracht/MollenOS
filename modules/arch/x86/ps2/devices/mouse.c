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

#include <io.h>
#include "mouse.h"
#include <gracht/link/vali.h>
#include "../ps2.h"
#include <string.h>
#include <event.h>
#include <os/keycodes.h>

extern gracht_server_t* __crt_get_module_server(void);

const int g_lsbTable[64] = {
    63, 30,  3, 32, 59, 14, 11, 33,
    60, 24, 50,  9, 55, 19, 21, 34,
    61, 29,  2, 53, 51, 23, 41, 18,
    56, 28,  1, 43, 46, 27,  0, 35,
    62, 31, 58,  4,  5, 49, 54,  6,
    15, 52, 12, 40,  7, 42, 45, 16,
    25, 57, 48, 13, 10, 39,  8, 44,
    20, 47, 38, 22, 17, 37, 36, 26
};

/**
 * bitScanForward
 * @author Matt Taylor (2003)
 * @param bb bitboard to scan
 * @precondition bb != 0
 * @return index (0..63) of least significant one bit
 */
int bitScanForward(uint64_t bb) {
    unsigned int folded;
    bb ^= bb - 1;
    folded = (int) bb ^ (bb >> 32);
    return g_lsbTable[folded * 0x78291ACF >> 26];
}

irqstatus_t
PS2MouseFastInterrupt(
        _In_ InterruptFunctionTable_t* InterruptTable,
        _In_ InterruptResourceTable_t* ResourceTable)
{
    DeviceIo_t*   ioSpace = INTERRUPT_IOSPACE(ResourceTable, 0);
    PS2Port_t*    port = (PS2Port_t*)INTERRUPT_RESOURCE(ResourceTable, 0);
    uint8_t       dataRecieved = (uint8_t)InterruptTable->ReadIoSpace(ioSpace, PS2_REGISTER_DATA, 1);
    uint8_t       breakAtBytes = port->device_data.mouse.mode == 0 ? 3 : 4;
    PS2Command_t* command = &port->ActiveCommand;

    if (command->State != PS2Free) {
        command->Buffer[command->SyncObject] = dataRecieved;
        smp_wmb();
        command->SyncObject++;
    }
    else {
        uint32_t index = port->ResponseWriteIndex;
        port->ResponseBuffer[index % PS2_RINGBUFFER_SIZE] = dataRecieved;
        smp_wmb();
        port->ResponseWriteIndex++;

        if (!(index % breakAtBytes)) {
            InterruptTable->EventSignal(ResourceTable->HandleResource);
        }
    }

    return IRQSTATUS_HANDLED;
}

static void __ParseButtonData(
        _In_ PS2Port_t*     port,
        _In_ const uint8_t* buffer)
{
    unsigned int lastButtons = port->device_data.mouse.buttonState;
    unsigned int buttons     = buffer[0] & 0x7; // L-M-R buttons
    unsigned int changed;

    if (port->device_data.mouse.mode == 2) {
        buttons |= (buffer[3] & (PS2_MOUSE_4BTN | PS2_MOUSE_5BTN)) >> 1;
    }

    // was it a button event?
    changed = buttons ^ lastButtons;
    if (changed) {
        uint16_t modifiers = 0;

        if (changed & lastButtons) {
            modifiers |= VK_MODIFIER_RELEASED;
        }

        port->device_data.mouse.buttonState = buttons;
        ctt_input_event_button_event_all(__crt_get_module_server(), port->DeviceId,
                                         VK_LBUTTON + bitScanForward(changed), modifiers);
    }
}

static void __ParseCursorData(
        _In_ PS2Port_t*     port,
        _In_ const uint8_t* buffer)
{
    // Update relative x and y
    int16_t rel_x = (int16_t)(buffer[1] - ((buffer[0] << 4) & 0x100));
    int16_t rel_y = (int16_t)(buffer[2] - ((buffer[0] << 3) & 0x100));
    int16_t rel_z = 0;

    // Check extended data modes
    if (port->device_data.mouse.mode == 1) {
        rel_z = (int16_t)(char)buffer[3];
    }
    else if (port->device_data.mouse.mode == 2) {
        // 4 bit signed value
        rel_z = (int16_t)(char)(buffer[3] & 0xF);
    }

    if (rel_x || rel_y || rel_z) {
        ctt_input_event_cursor_event_all(__crt_get_module_server(), port->DeviceId, 0, rel_x, rel_y, rel_z);
    }
}

void
PS2MouseInterrupt(
    _In_ PS2Port_t* port)
{
    uint8_t  bytesRequired = port->device_data.mouse.mode == 0 ? 3 : 4;
    uint32_t index         = port->ResponseReadIndex;

    // make sure there always are enough bytes to read
    smp_rmb();
    while (index <= (port->ResponseWriteIndex - bytesRequired)) {
        __ParseCursorData(port, &port->ResponseBuffer[index % PS2_RINGBUFFER_SIZE]);
        __ParseButtonData(port, &port->ResponseBuffer[index % PS2_RINGBUFFER_SIZE]);
        index += bytesRequired;
    }

    port->ResponseReadIndex = index;
}

oserr_t
PS2SetSampling(
    _In_ PS2Port_t* Port,
    _In_ uint8_t    Sampling)
{
    if (PS2PortExecuteCommand(Port, PS2_MOUSE_SETSAMPLE, NULL) != OS_EOK ||
        PS2PortExecuteCommand(Port, Sampling, NULL) != OS_EOK) {
        return OS_EUNKNOWN;
    }
    return OS_EOK;
}

oserr_t
PS2EnableExtensions(
    _In_ PS2Port_t* Port)
{
    uint8_t MouseId = 0;

    if (PS2SetSampling(Port, 200) != OS_EOK ||
        PS2SetSampling(Port, 200) != OS_EOK ||
        PS2SetSampling(Port, 80) != OS_EOK) {
        return OS_EUNKNOWN;
    }
    if (PS2PortExecuteCommand(Port, PS2_MOUSE_GETID, &MouseId) != OS_EOK) {
        return OS_EUNKNOWN;
    }

    if (MouseId == PS2_MOUSE_ID_EXTENDED2) {
        return OS_EOK;
    }
    else {
        return OS_EUNKNOWN;
    }
}

// The 'unlock' sequence of 200-100-80 sample
oserr_t
PS2EnableScroll(
    _In_ PS2Port_t* Port)
{
    uint8_t MouseId = 0;

    if (PS2SetSampling(Port, 200) != OS_EOK ||
        PS2SetSampling(Port, 100) != OS_EOK ||
        PS2SetSampling(Port, 80) != OS_EOK) {
        return OS_EUNKNOWN;
    }

    if (PS2PortExecuteCommand(Port, PS2_MOUSE_GETID, &MouseId) != OS_EOK) {
        return OS_EUNKNOWN;
    }

    if (MouseId == PS2_MOUSE_ID_EXTENDED) {
        return OS_EOK;
    }
    else {
        return OS_EUNKNOWN;
    }
}

oserr_t
PS2MouseInitialize(
    _In_ PS2Controller_t* controller,
    _In_ int              index)
{
    PS2Port_t* port = &controller->Ports[index];

    // Set initial mouse sampling
    memset(&port->device_data, 0, sizeof(port->device_data));
    port->device_data.mouse.sampling = 100;
    port->device_data.mouse.mode     = 0;

    // Initialize interrupt
    RegisterFastInterruptIoResource(&port->Interrupt, controller->Data);
    RegisterFastInterruptHandler(&port->Interrupt, (InterruptHandler_t)PS2MouseFastInterrupt);
    port->InterruptId = RegisterInterruptSource(&port->Interrupt, INTERRUPT_EXCLUSIVE);

    // The mouse is in default state at this point
    // since all ports suffer a reset - We want to test if the mouse is a 4-byte mouse
    if (PS2EnableScroll(port) == OS_EOK) {
        port->device_data.mouse.mode = 1;
        if (PS2EnableExtensions(port) == OS_EOK) {
            port->device_data.mouse.mode = 2;
        }
    }

    // Update sampling to 60, no need for faster updates
    if (PS2SetSampling(port, 60) == OS_EOK) {
        port->device_data.mouse.sampling = 100;
    }

    port->State = PortStateActive;
    return PS2PortExecuteCommand(port, PS2_ENABLE_SCANNING, NULL);
}

oserr_t
PS2MouseCleanup(
    _In_ PS2Controller_t* controller,
    _In_ int              index)
{
    PS2Port_t* port = &controller->Ports[index];

    // Try to disable the device before cleaning up
    PS2PortExecuteCommand(port, PS2_DISABLE_SCANNING, NULL);
    UnregisterInterruptSource(port->InterruptId);

    port->Signature = 0xFFFFFFFF;
    port->State     = PortStateConnected;
    return OS_EOK;
}

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

#include <ddk/utils.h>
#include <io.h>
#include "mouse.h"
#include <libwm_os.h>
#include "../ps2.h"
#include <string.h>
#include <stdlib.h>

InterruptStatus_t
PS2MouseFastInterrupt(
    _In_ FastInterruptResources_t* InterruptTable,
    _In_ void*                     NotUsed)
{
    DeviceIo_t* IoSpace     = INTERRUPT_IOSPACE(InterruptTable, 0);
    PS2Port_t* Port         = (PS2Port_t*)INTERRUPT_RESOURCE(InterruptTable, 0);
    uint8_t DataRecieved    = (uint8_t)InterruptTable->ReadIoSpace(IoSpace, PS2_REGISTER_DATA, 1);
    uint8_t BreakAtBytes    = PS2_MOUSE_DATA_MODE(Port) == 0 ? 3 : 4;
    PS2Command_t* Command   = &Port->ActiveCommand;

    if (Command->State != PS2Free) {
        Command->Buffer[Command->SyncObject] = DataRecieved;
        Command->SyncObject++;
        return InterruptHandledStop;
    }
    else {
        Port->ResponseBuffer[Port->ResponseWriteIndex++] = DataRecieved;
        if (!(Port->ResponseWriteIndex % BreakAtBytes)) {
            if (Port->ResponseWriteIndex == PS2_RINGBUFFER_SIZE) {
                Port->ResponseWriteIndex = 0; // Start over
            }
            return InterruptHandled;
        }
    }
    return InterruptHandledStop;
}

void
PS2MouseInterrupt(
    _In_ PS2Port_t* Port)
{
    struct hid_events_pointer_event_args Input;
    uint8_t BytesRequired = PS2_MOUSE_DATA_MODE(Port) == 0 ? 3 : 4;

    // Update relative x and y
    Input.rel_x = (int16_t)(Port->ResponseBuffer[Port->ResponseReadIndex + 1] - ((Port->ResponseBuffer[Port->ResponseReadIndex] << 4) & 0x100));
    Input.rel_y = (int16_t)(Port->ResponseBuffer[Port->ResponseReadIndex + 2] - ((Port->ResponseBuffer[Port->ResponseReadIndex] << 3) & 0x100));
    Input.buttons_set = Port->ResponseBuffer[0] & 0x7; // L-M-R buttons

    // Check extended data modes
    if (PS2_MOUSE_DATA_MODE(Port) == 1) {
        Input.rel_z = (int16_t)(char)Port->ResponseBuffer[Port->ResponseReadIndex + 3];
    }
    else if (PS2_MOUSE_DATA_MODE(Port) == 2) {
        // 4 bit signed value
        Input.rel_z = (int16_t)(char)(Port->ResponseBuffer[Port->ResponseReadIndex + 3] & 0xF);
        if (Port->ResponseBuffer[Port->ResponseReadIndex + 3] & PS2_MOUSE_4BTN) {
            Input.buttons_set |= 0x8;
        }
        if (Port->ResponseBuffer[Port->ResponseReadIndex + 3] & PS2_MOUSE_5BTN) {
            Input.buttons_set |= 0x10;
        }
    }
    else {
        Input.rel_z = 0;
    }
    Port->ResponseReadIndex += BytesRequired;
    if (Port->ResponseReadIndex == PS2_RINGBUFFER_SIZE) {
        Port->ResponseReadIndex = 0;
    }
    
    hid_events_pointer_event(Port->WmClient, 0 /* source */, 0 /* flags */, 
        Input.rel_x, Input.rel_y, Input.rel_z, Input.buttons_set);
}

OsStatus_t
PS2SetSampling(
    _In_ PS2Port_t* Port,
    _In_ uint8_t    Sampling)
{
    if (PS2PortExecuteCommand(Port, PS2_MOUSE_SETSAMPLE, NULL) != OsSuccess || 
        PS2PortExecuteCommand(Port, Sampling, NULL)            != OsSuccess) {
        return OsError;
    }
    return OsSuccess;
}

OsStatus_t
PS2EnableExtensions(
    _In_ PS2Port_t* Port)
{
    uint8_t MouseId = 0;

    if (PS2SetSampling(Port, 200) != OsSuccess || 
        PS2SetSampling(Port, 200) != OsSuccess || 
        PS2SetSampling(Port, 80)  != OsSuccess) {
        return OsError;
    }
    if (PS2PortExecuteCommand(Port, PS2_MOUSE_GETID, &MouseId) != OsSuccess) {
        return OsError;
    }

    if (MouseId == PS2_MOUSE_ID_EXTENDED2) {
        return OsSuccess;
    }
    else {
        return OsError;
    }
}

// The 'unlock' sequence of 200-100-80 sample
OsStatus_t
PS2EnableScroll(
    _In_ PS2Port_t* Port)
{
    uint8_t MouseId = 0;

    if (PS2SetSampling(Port, 200) != OsSuccess || 
        PS2SetSampling(Port, 100) != OsSuccess || 
        PS2SetSampling(Port, 80)  != OsSuccess) {
        return OsError;
    }

    if (PS2PortExecuteCommand(Port, PS2_MOUSE_GETID, &MouseId) != OsSuccess) {
        return OsError;
    }

    if (MouseId == PS2_MOUSE_ID_EXTENDED) {
        return OsSuccess;
    }
    else {
        return OsError;
    }
}

OsStatus_t
PS2MouseInitialize(
    _In_ PS2Controller_t* Controller,
    _In_ int              Port)
{
    wm_client_configuration_t wm_config;
    PS2Port_t*                Instance = &Controller->Ports[Port];

    // Set initial mouse sampling
    PS2_MOUSE_DATA_SAMPLING(Instance)   = 100;
    PS2_MOUSE_DATA_MODE(Instance)       = 0;

    // Start out by initializing the contract
    InitializeContract(&Instance->Contract, 
        Instance->Contract.DeviceId, 1, ContractInput, "PS2 Mouse Interface");
        
    // Register our contract for this device
    if (RegisterContract(&Instance->Contract) != OsSuccess) {
        ERROR("PS2-Mouse: failed to install contract");
        return OsError;
    }
    
    // Open up the input socket so we can send input data to the OS.
    wm_config.type = wm_client_packet_based;
    wm_os_get_server_packet_address(&wm_config.address, &wm_config.address_length);
    if (wm_client_create(&wm_config, &Instance->WmClient)) {
        ERROR("... [ps2] [mouse] [initialize] wm_client_initialize failed %i", errno);
    }
    
    // Initialize interrupt
    RegisterFastInterruptIoResource(&Instance->Interrupt, Controller->Data);
    RegisterFastInterruptHandler(&Instance->Interrupt, PS2MouseFastInterrupt);
    Instance->InterruptId = RegisterInterruptSource(&Instance->Interrupt, INTERRUPT_USERSPACE | INTERRUPT_NOTSHARABLE);

    // The mouse is in default state at this point
    // since all ports suffer a reset - We want to test if the mouse is a 4-byte mouse
    if (PS2EnableScroll(Instance) == OsSuccess) {
        PS2_MOUSE_DATA_MODE(Instance) = 1;
        if (PS2EnableExtensions(Instance) == OsSuccess) {
            PS2_MOUSE_DATA_MODE(Instance) = 2;
        }
    }

    // Update sampling to 60, no need for faster updates
    if (PS2SetSampling(Instance, 60) == OsSuccess) {
        PS2_MOUSE_DATA_SAMPLING(&Controller->Ports[Port]) = 100;
    }

    Instance->State = PortStateActive;
    return PS2PortExecuteCommand(Instance, PS2_ENABLE_SCANNING, NULL);
}

OsStatus_t
PS2MouseCleanup(
    _In_ PS2Controller_t* Controller,
    _In_ int              Port)
{
    PS2Port_t *Instance = &Controller->Ports[Port];

    // Try to disable the device before cleaning up
    PS2PortExecuteCommand(Instance, PS2_DISABLE_SCANNING, NULL);
    UnregisterInterruptSource(Instance->InterruptId);

    wm_client_shutdown(Instance->WmClient);
    Instance->Signature = 0xFFFFFFFF;
    Instance->State     = PortStateConnected;
    return OsSuccess;
}

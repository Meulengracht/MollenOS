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
#include <gracht/link/vali.h>
#include "../ps2.h"
#include <string.h>
#include <event.h>

InterruptStatus_t
PS2MouseFastInterrupt(
        _In_ InterruptFunctionTable_t* InterruptTable,
        _In_ InterruptResourceTable_t* ResourceTable)
{
    DeviceIo_t* IoSpace     = INTERRUPT_IOSPACE(ResourceTable, 0);
    PS2Port_t* Port         = (PS2Port_t*)INTERRUPT_RESOURCE(ResourceTable, 0);
    uint8_t DataRecieved    = (uint8_t)InterruptTable->ReadIoSpace(IoSpace, PS2_REGISTER_DATA, 1);
    uint8_t BreakAtBytes    = PS2_MOUSE_DATA_MODE(Port) == 0 ? 3 : 4;
    PS2Command_t* Command   = &Port->ActiveCommand;

    if (Command->State != PS2Free) {
        Command->Buffer[Command->SyncObject] = DataRecieved;
        Command->SyncObject++;
    }
    else {
        uint32_t index = atomic_fetch_add(&Port->ResponseWriteIndex, 1);
        Port->ResponseBuffer[index % PS2_RINGBUFFER_SIZE] = DataRecieved;
        if (!(index % BreakAtBytes)) {
            InterruptTable->EventSignal(ResourceTable->EventHandle);
        }
    }
    smp_wmb();
    return InterruptHandled;
}

void
PS2MouseInterrupt(
    _In_ PS2Port_t* port)
{
    struct ctt_input_cursor_event input;
    uint8_t                       bytesRequired = PS2_MOUSE_DATA_MODE(port) == 0 ? 3 : 4;
    uint32_t                      index;

    smp_rmb();
    index = atomic_load(&port->ResponseReadIndex) % PS2_RINGBUFFER_SIZE;

    // Update relative x and y
    input.rel_x       = (int16_t)(port->ResponseBuffer[index + 1] - ((port->ResponseBuffer[index] << 4) & 0x100));
    input.rel_y       = (int16_t)(port->ResponseBuffer[index + 2] - ((port->ResponseBuffer[index] << 3) & 0x100));
    input.buttons_set = port->ResponseBuffer[0] & 0x7; // L-M-R buttons

    // Check extended data modes
    if (PS2_MOUSE_DATA_MODE(port) == 1) {
        input.rel_z = (int16_t)(char)port->ResponseBuffer[index + 3];
    }
    else if (PS2_MOUSE_DATA_MODE(port) == 2) {
        // 4 bit signed value
        input.rel_z        = (int16_t)(char)(port->ResponseBuffer[index + 3] & 0xF);
        input.buttons_set |= (port->ResponseBuffer[index + 3] & (PS2_MOUSE_4BTN | PS2_MOUSE_5BTN)) >> 1;
    }
    else {
        input.rel_z = 0;
    }

    atomic_fetch_add(&port->ResponseReadIndex, bytesRequired);
    ctt_input_event_cursor_all(port->DeviceId, 0, input.rel_x, input.rel_y, input.rel_z, input.buttons_set);
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
    _In_ PS2Controller_t* controller,
    _In_ int              index)
{
    gracht_client_configuration_t      client_config;
    struct socket_client_configuration link_config;
    PS2Port_t*                         port = &controller->Ports[index];
    int                                status;

    // Set initial mouse sampling
    PS2_MOUSE_DATA_SAMPLING(port) = 100;
    PS2_MOUSE_DATA_MODE(port)     = 0;

    // Open up the input socket so we can send input data to the OS.
    link_config.type = gracht_link_packet_based;
    gracht_os_get_server_packet_address(&link_config.address, &link_config.address_length);
    
    status = gracht_link_socket_client_create(&client_config.link, &link_config);
    if (status) {
        ERROR("... [ps2] [mouse] [initialize] gracht_link_socket_client_create failed %i", errno);
    }
    
    if (status && gracht_client_create(&client_config, &port->GrachtClient)) {
        ERROR("... [ps2] [mouse] [initialize] gracht_client_create failed %i", errno);
    }

    port->event_descriptor = eventd((size_t)port, EVT_RESET_EVENT);
    if (port->event_descriptor <= 0) {
        ERROR("... [ps2] [mouse] [initialize] eventd failed %i", errno);
    }

    // Initialize interrupt
    RegisterInterruptEventDescriptor(&port->Interrupt, port->event_descriptor);
    RegisterFastInterruptIoResource(&port->Interrupt, controller->Data);
    RegisterFastInterruptHandler(&port->Interrupt, (InterruptHandler_t)PS2MouseFastInterrupt);
    port->InterruptId = RegisterInterruptSource(&port->Interrupt, INTERRUPT_USERSPACE | INTERRUPT_NOTSHARABLE);

    // The mouse is in default state at this point
    // since all ports suffer a reset - We want to test if the mouse is a 4-byte mouse
    if (PS2EnableScroll(port) == OsSuccess) {
        PS2_MOUSE_DATA_MODE(port) = 1;
        if (PS2EnableExtensions(port) == OsSuccess) {
            PS2_MOUSE_DATA_MODE(port) = 2;
        }
    }

    // Update sampling to 60, no need for faster updates
    if (PS2SetSampling(port, 60) == OsSuccess) {
        PS2_MOUSE_DATA_SAMPLING(&controller->Ports[index]) = 100;
    }

    port->State = PortStateActive;
    return PS2PortExecuteCommand(port, PS2_ENABLE_SCANNING, NULL);
}

OsStatus_t
PS2MouseCleanup(
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
    close(port->event_descriptor);
    return OsSuccess;
}

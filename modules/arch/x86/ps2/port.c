/**
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
 * X86 PS2 Controller (Controller) Driver
 * http://wiki.osdev.org/PS2
 */
//#define __TRACE

#include <ddk/busdevice.h>
#include <ddk/utils.h>
#include <threads.h>
#include <string.h>
#include <stdlib.h>
#include <event.h>
#include <ioset.h>
#include <gracht/server.h>
#include <ioctl.h>
#include "ps2.h"

extern gracht_server_t* __crt_get_module_server(void);

uint8_t
SendPS2PortCommand(
    _In_ int     Index,
    _In_ uint8_t Command)
{
    // Select the correct port
    if (Index != 0) {
        PS2SendCommand(PS2_SELECT_PORT2);
    }
    
    // Perform the self-test
    PS2WriteData(Command);
    return PS2ReadData(0);
}

oserr_t
PS2InterfaceTest(
    _In_ int Index)
{
    uint8_t Response;
    PS2SendCommand(Index == 0 ? PS2_INTERFACETEST_PORT1 : PS2_INTERFACETEST_PORT2);

    Response = PS2ReadData(0);
    return (Response == PS2_INTERFACETEST_OK) ? OS_EOK : OS_EUNKNOWN;
}

oserr_t
PS2ResetPort(
    _In_ int Index)
{
    uint8_t Response = SendPS2PortCommand(Index, PS2_RESET_PORT);

    // Check the response byte
    // Two results are ok, AA and FA
    if (Response == PS2_SELFTEST || Response == PS2_ACK) {
        (void)PS2ReadData(0); // We can recieve up to 3 bytes
        (void)PS2ReadData(0); // so don't ignore anything, but ignore errors
        return OS_EOK;
    }
    return OS_EUNKNOWN;
}

unsigned int
PS2IdentifyPort(
    _In_ int portIndex)
{
    uint8_t responseExtra;
    uint8_t response;

    response = SendPS2PortCommand(portIndex, PS2_DISABLE_SCANNING);
    if (response != PS2_ACK) {
        ERROR(" > failed to disable scanning for port %i, response 0x%x", portIndex, response);
        return 0xFFFFFFFF;
    }

    response = SendPS2PortCommand(portIndex, PS2_IDENTIFY_PORT);
    if (response != PS2_ACK) {
        ERROR(" > failed to identify port %i, response 0x%x", portIndex, response);
        return 0xFFFFFFFF;
    }

    // Read response byte
GetResponse:
    response = PS2ReadData(0);
    if (response == PS2_ACK) {
        goto GetResponse;
    }

    // Read next response byte
    responseExtra = PS2ReadData(0);
    if (responseExtra == 0xFF) {
        responseExtra = 0;
    }
    return (response << 8) | responseExtra;
}

/* PS2RegisterDevice
 * Shortcut function for registering a new device */
oserr_t
PS2RegisterDevice(
    _In_ PS2Port_t* port)
{
    BusDevice_t busDevice;

    memset(&busDevice, 0, sizeof(BusDevice_t));
    busDevice.Base.Id        = UUID_INVALID;
    busDevice.Base.ParentId  = UUID_INVALID;
    busDevice.Base.Length    = sizeof(BusDevice_t);

    busDevice.Base.VendorId  = 0xFFEF;
    busDevice.Base.ProductId = 0x0030;
    busDevice.Base.Class     = 0xFF0F;
    busDevice.Base.Subclass  = 0xFF0F;
    busDevice.Base.Identification.Description = "PS2 Child Device";

    // Initialize the irq structure
    busDevice.InterruptPin         = INTERRUPT_NONE;
    busDevice.InterruptAcpiConform = 0;

    // Select source from port index
    if (port->Index == 0) {
        busDevice.InterruptLine = PS2_PORT1_IRQ;
    }
    else {
        busDevice.InterruptLine = PS2_PORT2_IRQ;
    }

    // Lastly just register the device under the controller (todo)
    port->DeviceId = RegisterDevice(&busDevice.Base, DEVICE_REGISTER_FLAG_LOADDRIVER);
    if (port->DeviceId == UUID_INVALID) {
        ERROR("Failed to register new device");
        return OS_EUNKNOWN;
    }
    else {
        return OS_EOK;
    }
}

/* PS2PortWrite 
 * Writes the given data-byte to the ps2-port */
oserr_t
PS2PortWrite(
    _In_ PS2Port_t* Port,
    _In_ uint8_t    Value)
{
    // Select port 2 if neccessary
    if (Port->Index != 0) {
        PS2SendCommand(PS2_SELECT_PORT2);
    }
    return PS2WriteData(Value);
}

/* PS2PortWaitForState
 * Waits for the port to enter the given state. The function can return OS_EUNKNOWN
 * if the state is not reached in a seconds time. */
oserr_t
PS2PortWaitForState(
    _In_ PS2Port_t*         Port,
    _In_ PS2Command_t*      Command,
    _In_ PS2CommandState_t  State)
{
    volatile PS2CommandState_t* ActiveState;
    int Timeout = 1000;

    ActiveState = (volatile PS2CommandState_t*)&Command->State;
    while (*ActiveState != State && Timeout > 0) {
        thrd_sleep2(10);
        Timeout -= 10;

        // If it returns OS_EOK all done
        if (PS2PortFinishCommand(Port) == OS_EOK) {
            break;
        }
    }
    if (Timeout == 0) {
        WARNING("PS2-Command state timeout (%i != %i), command %u", 
            *ActiveState, State, Command->Command);
        return OS_EUNKNOWN;
    }
    return OS_EOK;
}

/* PS2PortExecuteCommand 
 * Executes the given ps2 command, handles both retries and commands that
 * require response. */
oserr_t
PS2PortExecuteCommand(
    _In_ PS2Port_t* port,
    _In_ uint8_t    commandValue,
    _In_ uint8_t*   response)
{
    PS2Command_t* command = &port->ActiveCommand;
    oserr_t    osStatus;

    // Initiate the packet data
    command->State      = PS2InQueue;
    command->RetryCount = 0;
    command->Command    = commandValue;
    command->Response   = response;
    command->SyncObject = 0;

    osStatus = PS2PortWrite(port, commandValue);
    if (osStatus != OS_EOK) {
        return osStatus;
    }
    return PS2PortWaitForState(port, command, PS2Free);
}

/* PS2PortFinishCommand 
 * Finalizes the current command and executes the next command in queue (if any). */
oserr_t
PS2PortFinishCommand(
    _In_ PS2Port_t* port)
{
    PS2Command_t* command = &port->ActiveCommand;
    switch (command->State) {
        case PS2InQueue: {
            if (command->SyncObject == 0) {
                return OS_EUNKNOWN;
            }

            if (command->Buffer[0] == PS2_RESEND_COMMAND) {
                if (command->RetryCount < PS2_MAX_RETRIES) {
                    command->RetryCount++;
                    command->SyncObject = 0; // Reset
                    PS2PortWrite(port, command->Command);
                    return OS_EUNKNOWN;
                }
                command->Command = PS2_FAILED_COMMAND;
                command->State   = PS2Free;
                // Go to next command
            }
            else if (command->Buffer[0] == PS2_ACK_COMMAND) {
                if (command->Response != NULL) {
                    command->State = PS2WaitingResponse;
                    return OS_EUNKNOWN;
                }
                command->State = PS2Free;
                // Go to next command
            }
            else {
                command->Command = PS2_FAILED_COMMAND;
                command->State   = PS2Free;
                // Go to next command
            }
        } break;

        case PS2WaitingResponse: {
            if (command->SyncObject == 1) {
                return OS_EUNKNOWN;
            }
            *(command->Response) = command->Buffer[1];
            command->State = PS2Free;
        } break;

        // Reached on PS2Free, should not happen
        default: {

        } break;
    }
    return OS_EOK;
}

/* PS2PortInitialize
 * Initializes the given port and tries to identify the device on the port */
oserr_t
PS2PortInitialize(
    _In_ PS2Port_t* port)
{
    uint8_t tempValue;
    int     opt = 1;

    TRACE("[PS2PortInitialize] %i", port->Index);

    // Initialize some variables for the port, this is essentially 
    // the same init as DeviceInterruptInitialize
    port->Interrupt.AcpiConform = 0;
    port->Interrupt.Pin         = INTERRUPT_NONE;
    port->Interrupt.Vectors[0]  = INTERRUPT_NONE;
    if (port->Index == 0) {
        port->Interrupt.Line = PS2_PORT1_IRQ;
    }
    else {
        port->Interrupt.Line = PS2_PORT2_IRQ;
    }

    port->event_descriptor = eventd(0, EVT_RESET_EVENT);
    if (port->event_descriptor <= 0) {
        ERROR("[PS2PortInitialize] eventd failed %i", errno);
        return OS_EUNKNOWN;
    }

    // register the event descriptor with the our server set
    ioset_ctrl(gracht_server_get_aio_handle(__crt_get_module_server()),
               IOSET_ADD, port->event_descriptor,
               &(struct ioset_event){ .data.context = port, .events = IOSETSYN });
    ioctl(port->event_descriptor, FIONBIO, &opt);

    // Initialize interrupt resources
    RegisterInterruptDescriptor(&port->Interrupt, port->event_descriptor);
    RegisterFastInterruptMemoryResource(&port->Interrupt, (uintptr_t)port, sizeof(PS2Port_t), 0);

    // Start out by doing an interface
    // test on the given port
    if (PS2InterfaceTest(port->Index) != OS_EOK) {
        ERROR(" > ps2-port %i failed interface test", port->Index);
        return OS_EUNKNOWN;
    }

    // Select the correct port
    PS2SendCommand(port->Index == 0 ? PS2_ENABLE_PORT1 : PS2_ENABLE_PORT2);

    // Get controller configuration
    PS2SendCommand(PS2_GET_CONFIGURATION);
    tempValue = PS2ReadData(0);

    // Check if the port is enabled - otherwise return error
    if (tempValue & (1 << (4 + port->Index))) {
        return OS_EUNKNOWN;
    }
    tempValue |= (1 << port->Index); // Enable IRQ

    // Write back the configuration
    PS2SendCommand(PS2_SET_CONFIGURATION);
    if (PS2WriteData(tempValue) != OS_EOK) {
        ERROR(" > ps2-port %i failed to update configuration", port->Index);
        return OS_EUNKNOWN;
    }

    // Reset the port
    if (PS2ResetPort(port->Index) != OS_EOK) {
        ERROR(" > ps2-port %i failed port reset", port->Index);
        return OS_EUNKNOWN;
    }
    port->Signature = PS2IdentifyPort(port->Index);
    TRACE(" > ps2-port %i device signature 0x%x", port->Index, port->Signature);
    
    // If the signature is ok - device present
    if (port->Signature != 0xFFFFFFFF) {
        port->State = PortStateConnected;
        return PS2RegisterDevice(port);
    }
    return OS_EOK;
}

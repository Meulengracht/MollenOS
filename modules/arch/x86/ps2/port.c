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
 * X86 PS2 Controller (Controller) Driver
 * http://wiki.osdev.org/PS2
 */
//#define __TRACE

#include <ddk/utils.h>
#include <threads.h>
#include <string.h>
#include <stdlib.h>
#include "ps2.h"

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

OsStatus_t
PS2InterfaceTest(
    _In_ int Index)
{
    uint8_t Response;
    PS2SendCommand(Index == 0 ? PS2_INTERFACETEST_PORT1 : PS2_INTERFACETEST_PORT2);

    Response = PS2ReadData(0);
    return (Response == PS2_INTERFACETEST_OK) ? OsSuccess : OsError;
}

OsStatus_t
PS2ResetPort(
    _In_ int Index)
{
    uint8_t Response = SendPS2PortCommand(Index, PS2_RESET_PORT);

    // Check the response byte
    // Two results are ok, AA and FA
    if (Response == PS2_SELFTEST || Response == PS2_ACK) {
        (void)PS2ReadData(0); // We can recieve up to 3 bytes
        (void)PS2ReadData(0); // so don't ignore anything, but ignore errors
        return OsSuccess;
    }
    return OsError;
}

unsigned int
PS2IdentifyPort(
    _In_ int Index)
{
    uint8_t ResponseExtra   = 0;
    uint8_t Response        = 0;

    Response = SendPS2PortCommand(Index, PS2_DISABLE_SCANNING);
    if (Response != PS2_ACK) {
        ERROR(" > failed to disable scanning for port %i, response 0x%x", Index, Response);
        return 0xFFFFFFFF;
    }

    Response = SendPS2PortCommand(Index, PS2_IDENTIFY_PORT);
    if (Response != PS2_ACK) {
        ERROR(" > failed to identify port %i, response 0x%x", Index, Response);
        return 0xFFFFFFFF;
    }

    // Read response byte
GetResponse:
    Response = PS2ReadData(0);
    if (Response == PS2_ACK) {
        goto GetResponse;
    }

    // Read next response byte
    ResponseExtra = PS2ReadData(0);
    if (ResponseExtra == 0xFF) {
        ResponseExtra = 0;
    }
    return (Response << 8) | ResponseExtra;
}

/* PS2RegisterDevice
 * Shortcut function for registering a new device */
OsStatus_t
PS2RegisterDevice(
    _In_ PS2Port_t* Port) 
{
    // Static Variables
    MCoreDevice_t Device = { 0 };
    Device.Length = sizeof(MCoreDevice_t);

    // Initialize VID/DID to us
    Device.VendorId = 0xFFEF;
    Device.DeviceId = 0x0030;

    // Invalidate generics
    Device.Class    = 0xFF0F;
    Device.Subclass = 0xFF0F;

    // Initialize the irq structure
    Device.Interrupt.Pin         = INTERRUPT_NONE;
    Device.Interrupt.Vectors[0]  = INTERRUPT_NONE;
    Device.Interrupt.AcpiConform = 0;

    // Select source from port index
    if (Port->Index == 0) {
        Device.Interrupt.Line = PS2_PORT1_IRQ;
    }
    else {
        Device.Interrupt.Line = PS2_PORT2_IRQ;
    }

    // Lastly just register the device under the controller (todo)
    Port->DeviceId = RegisterDevice(UUID_INVALID, &Device, 
        __DEVICEMANAGER_REGISTER_LOADDRIVER);    
    if (Port->DeviceId == UUID_INVALID) {
        ERROR("Failed to register new device");
        return OsError;
    }
    else {
        return OsSuccess;
    }
}

/* PS2PortWrite 
 * Writes the given data-byte to the ps2-port */
OsStatus_t
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
 * Waits for the port to enter the given state. The function can return OsError
 * if the state is not reached in a seconds time. */
OsStatus_t
PS2PortWaitForState(
    _In_ PS2Port_t*         Port,
    _In_ PS2Command_t*      Command,
    _In_ PS2CommandState_t  State)
{
    volatile PS2CommandState_t* ActiveState;
    volatile uint8_t* SyncObject;
    int Timeout = 1000;

    ActiveState = (volatile PS2CommandState_t*)&Command->State;
    SyncObject  = &Command->SyncObject;
    while (*ActiveState != State && Timeout > 0) {
        thrd_sleepex(10);
        Timeout -= 10;

        // If it returns OsSuccess all done
        if (PS2PortFinishCommand(Port) == OsSuccess) {
            break;
        }
    }
    if (Timeout == 0) {
        WARNING("PS2-Command state timeout (%i != %i), command %u", 
            *ActiveState, State, Command->Command);
        return OsError;
    }
    return OsSuccess;
}

/* PS2PortExecuteCommand 
 * Executes the given ps2 command, handles both retries and commands that
 * require response. */
OsStatus_t
PS2PortExecuteCommand(
    _In_ PS2Port_t* Port,
    _In_ uint8_t    Command,
    _In_ uint8_t*   Response)
{
    PS2Command_t *pCommand  = &Port->ActiveCommand;
    OsStatus_t Result       = OsSuccess;

    // Initiate the packet data
    pCommand->State         = PS2InQueue;
    pCommand->RetryCount    = 0;
    pCommand->Command       = Command;
    pCommand->Response      = Response;
    pCommand->SyncObject    = 0;
    Result                  = PS2PortWrite(Port, Command);
    return PS2PortWaitForState(Port, pCommand, PS2Free);
}

/* PS2PortFinishCommand 
 * Finalizes the current command and executes the next command in queue (if any). */
OsStatus_t
PS2PortFinishCommand(
    _In_ PS2Port_t*                 Port)
{
    PS2Command_t *pCommand  = &Port->ActiveCommand;
    switch (pCommand->State) {
        case PS2InQueue: {
            if (pCommand->SyncObject == 0) {
                return OsError;
            }

            if (pCommand->Buffer[0] == PS2_RESEND_COMMAND) {
                if (pCommand->RetryCount < PS2_MAX_RETRIES) {
                    pCommand->RetryCount++;
                    pCommand->SyncObject = 0; // Reset
                    PS2PortWrite(Port, pCommand->Command);
                    return OsError;
                }
                pCommand->Command   = PS2_FAILED_COMMAND;
                pCommand->State     = PS2Free;
                // Go to next command
            }
            else if (pCommand->Buffer[0] == PS2_ACK_COMMAND) {
                if (pCommand->Response != NULL) {
                    pCommand->State = PS2WaitingResponse;
                    return OsError;
                }
                pCommand->State = PS2Free;
                // Go to next command
            }
            else {
                pCommand->Command   = PS2_FAILED_COMMAND;
                pCommand->State     = PS2Free;
                // Go to next command
            }
        } break;

        case PS2WaitingResponse: {
            if (pCommand->SyncObject == 1) {
                return OsError;
            }
            *(pCommand->Response)   = pCommand->Buffer[1];
            pCommand->State         = PS2Free;
        } break;

        // Reached on PS2Free, should not happen
        default: {

        } break;
    }
    return OsSuccess;
}

/* PS2PortInitialize
 * Initializes the given port and tries to identify the device on the port */
OsStatus_t
PS2PortInitialize(
    _In_ PS2Port_t* Port)
{
    uint8_t Temp;

    TRACE(" > initializing ps2 port %i", Port->Index);

    // Initialize some variables for the port
    Port->Interrupt.AcpiConform = 0;
    Port->Interrupt.Pin         = INTERRUPT_NONE;
    Port->Interrupt.Vectors[0]  = INTERRUPT_NONE;
    if (Port->Index == 0) {
        Port->Interrupt.Line = PS2_PORT1_IRQ;
    }
    else {
        Port->Interrupt.Line = PS2_PORT2_IRQ;
    }

    // Initialize interrupt resources
    RegisterFastInterruptMemoryResource(&Port->Interrupt, (uintptr_t)Port, sizeof(PS2Port_t), 0);
    RegisterInterruptContext(&Port->Interrupt, Port);

    // Start out by doing an interface
    // test on the given port
    if (PS2InterfaceTest(Port->Index) != OsSuccess) {
        ERROR(" > ps2-port %i failed interface test", Port->Index);
        return OsError;
    }

    // Select the correct port
    PS2SendCommand(Port->Index == 0 ? PS2_ENABLE_PORT1 : PS2_ENABLE_PORT2);

    // Get controller configuration
    PS2SendCommand(PS2_GET_CONFIGURATION);
    Temp = PS2ReadData(0);

    // Check if the port is enabled - otherwise return error
    if (Temp & (1 << (4 + Port->Index))) {
        return OsError; 
    }
    Temp |= (1 << Port->Index); // Enable IRQ

    // Write back the configuration
    PS2SendCommand(PS2_SET_CONFIGURATION);
    if (PS2WriteData(Temp) != OsSuccess) {
        ERROR(" > ps2-port %i failed to update configuration", Port->Index);
        return OsError;
    }

    // Reset the port
    if (PS2ResetPort(Port->Index) != OsSuccess) {
        ERROR(" > ps2-port %i failed port reset", Port->Index);
        return OsError;
    }
    Port->Signature = PS2IdentifyPort(Port->Index);
    TRACE(" > ps2-port %i device signature 0x%x", Port->Index, Port->Signature);
    
    // If the signature is ok - device present
    if (Port->Signature != 0xFFFFFFFF) {
        Port->State = PortStateConnected;
        return PS2RegisterDevice(Port);
    }
    return OsSuccess;
}

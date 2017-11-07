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
 * MollenOS X86 PS2 Controller (Controller) Driver
 * http://wiki.osdev.org/PS2
 */

/* Includes 
 * - System */
#include <os/utils.h>
#include "ps2.h"

/* Includes
 * - Library */
#include <stddef.h>
#include <string.h>
#include <stdlib.h>

/* PS2InterfaceTest
 * Performs an interface test on the given port*/
OsStatus_t PS2InterfaceTest(int Port)
{
    // Variables
    uint8_t Response = 0;

    // Send command based on port
    if (Port == 0) {
        PS2SendCommand(PS2_INTERFACETEST_PORT1);
    }
    else {
        PS2SendCommand(PS2_INTERFACETEST_PORT2);
    }

    // Wait for ACK
    Response = PS2ReadData(0);

    // Sanitize the response byte
    return (Response == PS2_INTERFACETEST_OK) ? OsSuccess : OsError;
}

/* PS2ResetPort
 * Resets the given port and tests for a reset-ok */
OsStatus_t PS2ResetPort(int Index)
{
    // Variables
    uint8_t Response = 0;

    // Select the correct port
    if (Index != 0) {
        PS2SendCommand(PS2_SELECT_PORT2);
    }
    
    // Perform the self-test
    PS2WriteData(PS2_RESET_PORT);
    Response = PS2ReadData(0);

    // Check the response byte
    // Two results are ok, AA and FA
    if (Response == PS2_SELFTEST
        || Response == PS2_ACK) {
        Response = PS2ReadData(1);
        Response = PS2ReadData(1);
        return OsSuccess;
    }
    else {
        return OsError;
    }
}

/* PS2IdentifyPort
 * Identifies the device currently connected to the
 * given port index, if fails it returns 0xFFFFFFFF */
DevInfo_t PS2IdentifyPort(int Index)
{
    // Variables
    uint8_t Response = 0, ResponseExtra = 0;

    // Select correct port
    if (Index != 0) {
        PS2SendCommand(PS2_SELECT_PORT2);
    }

    // Disable scanning when identifying
    if (PS2WriteData(PS2_DISABLE_SCANNING) != OsSuccess) {
        return 0xFFFFFFFF;
    }

    // Wait for ACK
    Response = PS2ReadData(0);

    // Sanitize result
    if (Response != PS2_ACK) {
        return 0xFFFFFFFF;
    }

    // Select correct port
    if (Index != 0) {
        PS2SendCommand(PS2_SELECT_PORT2);
    }

    // Send the identify command
    if (PS2WriteData(PS2_IDENTIFY_PORT) != OsSuccess) {
        return 0xFFFFFFFF;
    }

    // Wait for ACK
    Response = PS2ReadData(0);

    // Sanitize result
    if (Response != PS2_ACK) {
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

    // Combine bytes
    return (Response << 8) | ResponseExtra;
}

/* PS2RegisterDevice
 * Shortcut function for registering a new device */
OsStatus_t
PS2RegisterDevice(
    _In_ PS2Port_t *Port) 
{
    // Static Variables
    MCoreDevice_t Device = { 0 };
    Device.Length = sizeof(MCoreDevice_t);

    // Initialize VID/DID to us
    Device.VendorId = 0xFFEF;
    Device.DeviceId = 0x0030;

    // Invalidate generics
    Device.Class = 0xFF0F;
    Device.Subclass = 0xFF0F;

    // Initialize the irq structure
    Device.Interrupt.Pin = INTERRUPT_NONE;
    Device.Interrupt.Vectors[0] = INTERRUPT_NONE;
    Device.Interrupt.AcpiConform = 0;

    // Select source from port index
    if (Port->Index == 0) {
        Device.Interrupt.Line = PS2_PORT1_IRQ;
    }
    else {
        Device.Interrupt.Line = PS2_PORT2_IRQ;
    }

    // Lastly just register the device under the controller (todo)
    Port->Contract.DeviceId = RegisterDevice(UUID_INVALID, &Device, 
        __DEVICEMANAGER_REGISTER_LOADDRIVER);    
    if (Port->Contract.DeviceId == UUID_INVALID) {
        ERROR("Failed to register new device");
        return OsError;
    }
    else {
        return OsSuccess;
    }
}

/* PS2PortWrite 
 * Writes the given data-byte to the ps2-port */
OsStatus_t PS2PortWrite(PS2Port_t *Port, uint8_t Value)
{
    // Select port 2 if neccessary
    if (Port->Index != 0) {
        PS2SendCommand(PS2_SELECT_PORT2);
    }

    // Write the data
    return PS2WriteData(Value);
}

/* PS2PortQueueCommand 
 * Queues the given command up for the given port
 * if a response is needed for the previous commnad
 * Set command = PS2_RESPONSE_COMMAND and pointer to response buffer */
OsStatus_t PS2PortQueueCommand(PS2Port_t *Port, uint8_t Command, uint8_t *Response)
{
    // Variables
    PS2Command_t *pCommand = NULL;
    OsStatus_t Result = OsSuccess;
    int Execute = 0;
    int i;

    // Find a free command spot for the queue
FindCommand:
    for (i = 0; i < PS2_MAXCOMMANDS; i++) {
        if (Port->Commands[i].InUse == 0) {
            pCommand = &Port->Commands[i];
            if (Port->CommandIndex == -1) {
                Port->CommandIndex = i;
                Execute = 1;
            }
            break;
        }
    }

    // Did we find one? Or try again?
    if (pCommand == NULL) {
        goto FindCommand;
    }

    // Initiate the packet data
    pCommand->Executed = 0;
    pCommand->Step = 0;
    pCommand->Command = Command;
    pCommand->Response = Response;
    pCommand->InUse = 1;

    // Is the queue already running?
    // Otherwise start it by sending the command
    if (Execute == 1) {
        Result = PS2PortWrite(Port, Command);
    }

    // Asynchronously? Or do we need response?
    if (Response != NULL) {
        while (pCommand->Executed != 2);
    }

    // Queued successfully 
    return Result;
}

/* PS2PortFinishCommand 
 * Finalizes the current command and executes
 * the next command in queue (if any). */
OsStatus_t PS2PortFinishCommand(PS2Port_t *Port, uint8_t Result)
{
    // Variables
    int Start = Port->CommandIndex + 1;
    int i;

    // Always handle ACK's first
    if (Result == PS2_ACK_COMMAND
        && Port->CommandIndex != -1) {
        Port->Commands[Port->CommandIndex].Executed = 1;

        // Does it need a response byte as-well?
        // Otherwise we can handle it directly now
        if (Port->Commands[Port->CommandIndex].Response == NULL) {
            Port->Commands[Port->CommandIndex].InUse = 0;

            // Find next queued command from current position
            for (i = Start; i < PS2_MAXCOMMANDS; i++) {
                if (Port->Commands[i].InUse == 1) {
                    Port->CommandIndex = i;
                    PS2PortWrite(Port, Port->Commands[Port->CommandIndex].Command);
                    break;
                }
                else {
                    Port->CommandIndex = PS2_NO_COMMAND;
                }
            }

            // Find next queued command from start
            if (i == PS2_MAXCOMMANDS) {
                for (i = 0; i < Start; i++) {
                    if (Port->Commands[i].InUse == 1) {
                        Port->CommandIndex = i;
                        PS2PortWrite(Port, Port->Commands[Port->CommandIndex].Command);
                        break;
                    }
                    else {
                        Port->CommandIndex = PS2_NO_COMMAND;
                    }
                }
            }
        }
        return OsSuccess;
    }
    else if (Result == PS2_RESEND_COMMAND
        && Port->CommandIndex != -1) {
        Port->Commands[Port->CommandIndex].Step++;
        if (Port->Commands[Port->CommandIndex].Step != 3) {
            PS2PortWrite(Port, Port->Commands[Port->CommandIndex].Command);
        }
        else {
            Port->Commands[Port->CommandIndex].Executed = 2;
            Port->Commands[Port->CommandIndex].InUse = 0;

            // Find next queued command from current position
            for (i = Start; i < PS2_MAXCOMMANDS; i++) {
                if (Port->Commands[i].InUse == 1) {
                    Port->CommandIndex = i;
                    PS2PortWrite(Port, Port->Commands[Port->CommandIndex].Command);
                    break;
                }
                else {
                    Port->CommandIndex = PS2_NO_COMMAND;
                }
            }

            // Find next queued command from start
            if (i == PS2_MAXCOMMANDS) {
                for (i = 0; i < Start; i++) {
                    if (Port->Commands[i].InUse == 1) {
                        Port->CommandIndex = i;
                        PS2PortWrite(Port, Port->Commands[Port->CommandIndex].Command);
                        break;
                    }
                    else {
                        Port->CommandIndex = PS2_NO_COMMAND;
                    }
                }
            }
        }
        return OsSuccess;
    }
    else if (Port->CommandIndex != -1) {
        Port->Commands[Port->CommandIndex].Executed = 2;
        Port->Commands[Port->CommandIndex].InUse = 0;

        // Sanitize whether or not there should be a response
        if (Port->Commands[Port->CommandIndex].Response != NULL) {
            *(Port->Commands[Port->CommandIndex].Response) = Result;
        }

        // Find next queued command from current position
        for (i = Start; i < PS2_MAXCOMMANDS; i++) {
            if (Port->Commands[i].InUse == 1) {
                Port->CommandIndex = i;
                PS2PortWrite(Port, Port->Commands[Port->CommandIndex].Command);
                break;
            }
            else {
                Port->CommandIndex = PS2_NO_COMMAND;
            }
        }

        // Find next queued command from start
        if (i == PS2_MAXCOMMANDS) {
            for (i = 0; i < Start; i++) {
                if (Port->Commands[i].InUse == 1) {
                    Port->CommandIndex = i;
                    PS2PortWrite(Port, Port->Commands[Port->CommandIndex].Command);
                    break;
                }
                else {
                    Port->CommandIndex = PS2_NO_COMMAND;
                }
            }
        }
        return OsSuccess;
    }
    else {
        return OsError;
    }
}

/* PS2PortInitialize
 * Initializes the given port and tries
 * to identify the device on the port */
OsStatus_t PS2PortInitialize(PS2Port_t *Port)
{
    // Variables
    uint8_t Temp = 0;

    // Initialize some variables for the port
    Port->CommandIndex = PS2_NO_COMMAND;

    // Start out by doing an interface
    // test on the given port
    if (PS2InterfaceTest(Port->Index) != OsSuccess) {
        ERROR("PS2-Port (%i): Failed interface test", Port->Index);
        return OsError;
    }

    // Select the correct port
    if (Port->Index == 0) {
        PS2SendCommand(PS2_ENABLE_PORT1);
    }
    else {
        PS2SendCommand(PS2_ENABLE_PORT2);
    }

    // Get controller configuration
    PS2SendCommand(PS2_GET_CONFIGURATION);
    Temp = PS2ReadData(0);

    // Check if the port is enabled
    // Otherwise return error
    if (Temp & (1 << (4 + Port->Index))) {
        return OsError; 
    }

    // Enable IRQ
    Temp |= (1 << Port->Index);

    // Write back the configuration
    PS2SendCommand(PS2_SET_CONFIGURATION);
    if (PS2WriteData(Temp) != OsSuccess) {
        ERROR("PS2-Port (%i): Failed to update configuration", Port->Index);
        return OsError;
    }

    // Reset the port
    if (PS2ResetPort(Port->Index) != OsSuccess) {
        ERROR("PS2-Port (%i): Failed port reset", Port->Index);
        return OsError;
    }

    // Identify type of device on port
    Port->Signature = PS2IdentifyPort(Port->Index);
    
    // If the signature is ok - device present
    if (Port->Signature != 0xFFFFFFFF) {
        Port->Connected = 1;
    }

    // Lastly register device on port
    return PS2RegisterDevice(Port);
}

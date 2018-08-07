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

#include <os/contracts/base.h>
#include <os/mollenos.h>
#include <os/utils.h>
#include <string.h>
#include <stdlib.h>
#include "ps2.h"

static PS2Controller_t *Ps2Controller = NULL;

/* PS2ReadStatus
 * Reads the status byte from the controller */
uint8_t
PS2ReadStatus(void)
{
    return (uint8_t)ReadDeviceIo(Ps2Controller->Command, PS2_REGISTER_STATUS, 1);
}

/* PS2Wait
 * Waits for the given flag to clear by doing a number of iterations giving a fake-delay */
OsStatus_t
PS2Wait(
    _In_ uint8_t     Flags,
    _In_ int         Negate)
{
    // Wait for up to 1000 iterations
    for (int i = 0; i < 1000; i++) {
        if (Negate == 1) {
            if (PS2ReadStatus() & Flags) {
                return OsSuccess;
            }
        }
        else {
            if (!(PS2ReadStatus() & Flags)) {
                return OsSuccess;
            }
        }    
    }
    return OsError; // If we reach here - it never cleared
}

/* PS2ReadData
 * Reads a byte from the PS2 controller data port */
uint8_t
PS2ReadData(
    _In_ int         Dummy)
{
    // Only wait for input to be full in case
    // we don't do dummy reads
    if (Dummy == 0) {
        if (PS2Wait(PS2_STATUS_OUTPUT_FULL, 1) == OsError) {
            return 0xFF;
        }
    }
    return (uint8_t)ReadDeviceIo(Ps2Controller->Data, PS2_REGISTER_DATA, 1);
}

/* PS2WriteData
 * Writes a data byte to the PS2 controller data port */
OsStatus_t
PS2WriteData(
    _In_ uint8_t     Value)
{
    // Sanitize buffer status
    if (PS2Wait(PS2_STATUS_INPUT_FULL, 0) != OsSuccess) {
        return OsError;
    }
    WriteDeviceIo(Ps2Controller->Data, PS2_REGISTER_DATA, Value, 1);
    return OsSuccess;
}

/* PS2SendCommand
 * Writes the given command to the ps2-controller */
void
PS2SendCommand(
    _In_ uint8_t     Command)
{
    // Wait for flag to clear, then write data
    PS2Wait(PS2_STATUS_INPUT_FULL, 0);
    WriteDeviceIo(Ps2Controller->Command, PS2_REGISTER_COMMAND, Command, 1);
}

/* PS2SetScanning
 * Updates the enable/disable status of the port */
OsStatus_t
PS2SetScanning(
    _In_ int         Index,
    _In_ uint8_t     Status)
{
    // Always select port if neccessary
    if (Index != 0) {
        PS2SendCommand(PS2_SELECT_PORT2);
    }

    // Set sample rate to given value
    if (PS2WriteData(Status) != OsSuccess || PS2ReadData(0) != PS2_ACK) {
        return OsError;
    }
    return OsSuccess;
}

/* PS2SelfTest
 * Does 5 tries to perform a self-test of the ps2 controller */
OsStatus_t
PS2SelfTest(void)
{
    uint8_t Temp     = 0;
    int i             = 0;

    // Iterate through 5 tries
    for (; i < 5; i++) {
        PS2SendCommand(PS2_SELFTEST);
        Temp = PS2ReadData(0);
        if (Temp == PS2_SELFTEST_OK) {
            break;
        }
    }
    return (i == 5) ? OsError : OsSuccess;
}

/* PS2Initialize 
 * Initializes the controller and initializes the attached ports */
OsStatus_t
PS2Initialize(
    _In_ MCoreDevice_t*    Device)
{
    OsStatus_t Status = OsSuccess;
    uint8_t Temp;
    int i;

    // Store a copy of the device
    memcpy(&Ps2Controller->Device, Device, sizeof(MCoreDevice_t));

    // Initialize the ps2-contract
    InitializeContract(&Ps2Controller->Controller, Device->Id, 1,
        ContractController, "PS2 Controller Interface");

    // No problem, last thing is to acquire the
    // io-spaces, and just return that as result
    if (AcquireDeviceIo(&Ps2Controller->Device.IoSpaces[0]) != OsSuccess || 
        AcquireDeviceIo(&Ps2Controller->Device.IoSpaces[1]) != OsSuccess) {
        ERROR(" > failed to acquire ps2 io spaces");
        return OsError;
    }

    // Data is at 0x60 - the first space, Command is at 0x64, the second space
    Ps2Controller->Data     = &Ps2Controller->Device.IoSpaces[0];
    Ps2Controller->Command  = &Ps2Controller->Device.IoSpaces[1];
    RegisterContract(&Ps2Controller->Controller);

    // Dummy reads, empty it's buffer
    PS2ReadData(1);
    PS2ReadData(1);

    // Disable Devices
    PS2SendCommand(PS2_DISABLE_PORT1);
    PS2SendCommand(PS2_DISABLE_PORT2);

    // Make sure it's empty, now devices cant fill it
    PS2ReadData(1);

    // Get Controller Configuration
    PS2SendCommand(PS2_GET_CONFIGURATION);
    Temp = PS2ReadData(0);

    // Discover port status 
    // both ports should be disabled
    Ps2Controller->Ports[0].Enabled = 1;
    if (!(Temp & PS2_CONFIG_PORT2_DISABLED)) {
        Ps2Controller->Ports[1].Enabled = 0;
    }
    else {
        // This simply means we should test channel 2
        Ps2Controller->Ports[1].Enabled = 1;
    }

    // Clear all irqs and translations
    Temp &= ~(PS2_CONFIG_PORT1_IRQ | PS2_CONFIG_PORT2_IRQ
        | PS2_CONFIG_TRANSLATION);

    // Write back the configuration
    PS2SendCommand(PS2_SET_CONFIGURATION);
    Status = PS2WriteData(Temp);

    // Perform Self Test
    if (Status != OsSuccess || PS2SelfTest() != OsSuccess) {
        ERROR(" > failed to initialize ps2 controller, giving up");
        return OsError;
    }

    // Initialize the ports
    for (i = 0; i < PS2_MAXPORTS; i++) {
        Ps2Controller->Ports[i].Index = i;
        if (Ps2Controller->Ports[i].Enabled == 1) {
            Status = PS2PortInitialize(&Ps2Controller->Ports[i]);
            if (Status != OsSuccess) {
                ERROR(" > failed to setup ps2 port %i", i);
            }
        }
    }
    return OsSuccess;
}

/* OnInterrupt
 * Is called when one of the registered devices produces an interrupt. On successful handled
 * interrupt return OsSuccess, otherwise the interrupt won't be acknowledged */
InterruptStatus_t
OnInterrupt(
    _In_Opt_ void*  InterruptData,
    _In_Opt_ size_t Arg0,
    _In_Opt_ size_t Arg1,
    _In_Opt_ size_t Arg2)
{
    PS2Port_t* Port = (PS2Port_t*)InterruptData;
    _CRT_UNUSED(Arg0);
    _CRT_UNUSED(Arg1);
    _CRT_UNUSED(Arg2);

    if (Port->Signature == 0xAB41 || Port->Signature == 0xABC1 ||
        Port->Signature == 0xAB83) {
        return PS2KeyboardInterrupt(Port);
    }
    else if (Port->Signature != 0xFFFFFFFF) {
        return PS2MouseInterrupt(Port);
    }
    return InterruptHandled;
}

/* OnTimeout
 * Is called when one of the registered timer-handles
 * times-out. A new timeout event is generated and passed on to the below handler */ 
OsStatus_t
OnTimeout(
    _In_ UUId_t     Timer,
    _In_ void*        Data)
{
    _CRT_UNUSED(Timer);
    _CRT_UNUSED(Data);
    return OsSuccess;
}

/* OnLoad
 * The entry-point of a driver, this is called
 * as soon as the driver is loaded in the system */
OsStatus_t
OnLoad(void)
{
    // Allocate a new instance of the ps2-data
    Ps2Controller = (PS2Controller_t*)malloc(sizeof(PS2Controller_t));
    memset(Ps2Controller, 0, sizeof(PS2Controller_t));
    Ps2Controller->Device.Id = UUID_INVALID;
    return OsSuccess;
}

/* OnUnload
 * This is called when the driver is being unloaded
 * and should free all resources allocated by the system */
OsStatus_t
OnUnload(void)
{
    // Destroy the io-spaces we created
    if (Ps2Controller->Command != NULL) {
        ReleaseDeviceIo(Ps2Controller->Command);
    }

    if (Ps2Controller->Data != NULL) {
        ReleaseDeviceIo(Ps2Controller->Data);
    }
    free(Ps2Controller);
    return OsSuccess;
}

/* OnRegister
 * Is called when the device-manager registers a new
 * instance of this driver for the given device */
OsStatus_t
OnRegister(
    _In_ MCoreDevice_t*    Device)
{
    OsStatus_t Result = OsSuccess;
    PS2Port_t *Port;

    // First register call is the ps2-controller and all sequent calls here is ps2-devices 
    // So install the contract as soon as it arrives
    if (Ps2Controller->Device.Id == UUID_INVALID) {
        return PS2Initialize(Device);
    }

    // Select port from device-id
    if (Ps2Controller->Ports[0].Contract.DeviceId == Device->Id) {
        Port = &Ps2Controller->Ports[0];
    }
    else if (Ps2Controller->Ports[1].Contract.DeviceId == Device->Id) {
        Port = &Ps2Controller->Ports[1];
    }
    else {
        return OsError;
    }

    // Ok .. It's a new device 
    // - What kind of device?
    if (Port->Signature == 0xAB41 || Port->Signature == 0xABC1) { // MF2 Keyboard Translation
        Result = PS2KeyboardInitialize(Ps2Controller, Port->Index, 1);
        if (Result != OsSuccess) {
            ERROR(" > failed to initalize ps2-keyboard");
        }
    }
    else if (Port->Signature == 0xAB83) { // MF2 Keyboard
        Result = PS2KeyboardInitialize(Ps2Controller, Port->Index, 0);
        if (Result != OsSuccess) {
            ERROR(" > failed to initalize ps2-keyboard");
        }
    }
    else if (Port->Signature != 0xFFFFFFFF) {
        Result = PS2MouseInitialize(Ps2Controller, Port->Index);
        if (Result != OsSuccess) {
            ERROR(" > failed to initalize ps2-mouse");
        }
    }
    else {
        Result = OsError;
    }
    return Result;
}

/* OnUnregister
 * Is called when the device-manager wants to unload
 * an instance of this driver from the system */
OsStatus_t
OnUnregister(
    _In_ MCoreDevice_t*    Device)
{
    OsStatus_t Result = OsError;
    PS2Port_t *Port;

    // Select port from device-id
    if (Ps2Controller->Ports[0].Contract.DeviceId == Device->Id) {
        Port = &Ps2Controller->Ports[0];
    }
    else if (Ps2Controller->Ports[1].Contract.DeviceId == Device->Id) {
        Port = &Ps2Controller->Ports[1];
    }
    else {
        return OsError;    // Probably the controller itself
    }

    // Handle device destruction
    if (Port->Signature == 0xAB41
        || Port->Signature == 0xABC1) { // MF2 Keyboard Translation
        Result = PS2KeyboardCleanup(Ps2Controller, Port->Index);
    }
    else if (Port->Signature == 0xAB83) { // MF2 Keyboard
        Result = PS2KeyboardCleanup(Ps2Controller, Port->Index);
    }
    else if (Port->Signature != 0xFFFFFFFF) {
        Result = PS2MouseCleanup(Ps2Controller, Port->Index);
    }
    return Result;
}

/* OnQuery
 * Occurs when an external process or server quries
 * this driver for data, this will correspond to the query
 * function that is defined in the contract */
OsStatus_t 
OnQuery(
    _In_     MContractType_t        QueryType, 
    _In_     int                    QueryFunction, 
    _In_Opt_ MRemoteCallArgument_t* Arg0,
    _In_Opt_ MRemoteCallArgument_t* Arg1,
    _In_Opt_ MRemoteCallArgument_t* Arg2,
    _In_     MRemoteCallAddress_t*  Address)
{
    // You can't query the ps-2 driver
    _CRT_UNUSED(QueryType);
    _CRT_UNUSED(QueryFunction);
    _CRT_UNUSED(Arg0);
    _CRT_UNUSED(Arg1);
    _CRT_UNUSED(Arg2);
    _CRT_UNUSED(Address);
    return OsSuccess;
}

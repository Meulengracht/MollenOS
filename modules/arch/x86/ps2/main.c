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
 * X86 PS2 Controller (Controller) Driver
 * http://wiki.osdev.org/PS2
 */

#include <ddk/service.h>
#include <ddk/utils.h>
#include <ddk/convert.h>
#include <ioset.h>
#include <string.h>
#include <stdlib.h>
#include <threads.h>

#include "ps2.h"

#include <ctt_driver_service_server.h>
#include <ctt_input_service_server.h>

extern gracht_server_t* __crt_get_module_server(void);

static PS2Controller_t* Ps2Controller = NULL;

uint8_t
PS2ReadStatus(void)
{
    return (uint8_t)ReadDeviceIo(Ps2Controller->Command, PS2_REGISTER_STATUS, 1);
}

oserr_t
WaitForPs2StatusFlagsSet(
    _In_ uint8_t Flags)
{
    int Timeout = 100; // 100 ms
    while (Timeout > 0) {
        if (PS2ReadStatus() & Flags) {
            return OS_EOK;
        }
        thrd_sleep(&(struct timespec) { .tv_nsec = 5 * NSEC_PER_MSEC }, NULL);;
        Timeout -= 5;
    }
    return OS_EUNKNOWN; // If we reach here - it never set
}

oserr_t
WaitForPs2StatusFlagsClear(
    _In_ uint8_t Flags)
{
    int Timeout = 100; // 100 ms
    while (Timeout > 0) {
        if (!(PS2ReadStatus() & Flags)) {
            return OS_EOK;
        }
        thrd_sleep(&(struct timespec) { .tv_nsec = 5 * NSEC_PER_MSEC }, NULL);
        Timeout -= 5;
    }
    return OS_EUNKNOWN; // If we reach here - it never set
}

uint8_t
PS2ReadData(
    _In_ int Dummy)
{
    // Only wait for input to be full in case we don't do dummy reads
    if (Dummy == 0) {
        if (WaitForPs2StatusFlagsSet(PS2_STATUS_OUTPUT_FULL) == OS_EUNKNOWN) {
            return 0xFF;
        }
    }
    return (uint8_t)ReadDeviceIo(Ps2Controller->Data, PS2_REGISTER_DATA, 1);
}

oserr_t
PS2WriteData(
    _In_ uint8_t Value)
{
    if (WaitForPs2StatusFlagsClear(PS2_STATUS_INPUT_FULL) != OS_EOK) {
        return OS_EUNKNOWN;
    }
    WriteDeviceIo(Ps2Controller->Data, PS2_REGISTER_DATA, Value, 1);
    return OS_EOK;
}

void
PS2SendCommand(
    _In_ uint8_t Command)
{
    // Wait for flag to clear, then write data
    if (WaitForPs2StatusFlagsClear(PS2_STATUS_INPUT_FULL) != OS_EOK) {
        ERROR(" > input buffer full flags never cleared");
    }
    WriteDeviceIo(Ps2Controller->Command, PS2_REGISTER_COMMAND, Command, 1);
}

oserr_t
PS2SetScanning(
    _In_ int     Index,
    _In_ uint8_t Status)
{
    // Always select port if neccessary
    if (Index != 0) {
        PS2SendCommand(PS2_SELECT_PORT2);
    }

    if (PS2WriteData(Status) != OS_EOK ||
        PS2ReadData(0)   != PS2_ACK) {
        return OS_EUNKNOWN;
    }
    return OS_EOK;
}

oserr_t
PS2SelfTest(void)
{
    uint8_t Temp = 0;
    int     i    = 0;

    // Iterate through 5 tries
    for (; i < 5; i++) {
        PS2SendCommand(PS2_SELFTEST);
        Temp = PS2ReadData(0);
        if (Temp == PS2_SELFTEST_OK) {
            break;
        }
    }
    return (i == 5) ? OS_EUNKNOWN : OS_EOK;
}

oserr_t
PS2Initialize(
    _In_ Device_t* device)
{
    oserr_t oserr;
    uint8_t temp;
    int     i;

    Ps2Controller->Device = (BusDevice_t*)device;

    // No problem, last thing is to acquire the io-spaces, and just return that as result
    if (AcquireDeviceIo(&Ps2Controller->Device->IoSpaces[0]) != OS_EOK ||
        AcquireDeviceIo(&Ps2Controller->Device->IoSpaces[1]) != OS_EOK) {
        ERROR(" > failed to acquire ps2 io spaces");
        return OS_EUNKNOWN;
    }

    // Data is at 0x60 - the first space, Command is at 0x64, the second space
    Ps2Controller->Data    = &Ps2Controller->Device->IoSpaces[0];
    Ps2Controller->Command = &Ps2Controller->Device->IoSpaces[1];

    // Disable Devices
    PS2SendCommand(PS2_DISABLE_PORT1);
    PS2SendCommand(PS2_DISABLE_PORT2);

    // Dummy reads, empty it's buffer
    PS2ReadData(1);
    PS2ReadData(1);

    // Get Controller Configuration
    PS2SendCommand(PS2_GET_CONFIGURATION);
    temp = PS2ReadData(0);

    // Discover port status - initially both ports should be enabled
    // we will detect this later on
    Ps2Controller->Ports[0].State = PortStateEnabled;
    Ps2Controller->Ports[1].State = PortStateEnabled;

    // Clear all irqs and translations
    temp &= ~(PS2_CONFIG_PORT1_IRQ | PS2_CONFIG_PORT2_IRQ | PS2_CONFIG_TRANSLATION);

    // Write back the configuration
    PS2SendCommand(PS2_SET_CONFIGURATION);
    oserr = PS2WriteData(temp);

    // Perform Self Test
    if (oserr != OS_EOK || PS2SelfTest() != OS_EOK) {
        ERROR(" > failed to initialize ps2 controller, giving up");
        return OS_EUNKNOWN;
    }

    // Initialize the ports
    for (i = 0; i < PS2_MAXPORTS; i++) {
        Ps2Controller->Ports[i].Index   = i;
        oserr = PS2PortInitialize(&Ps2Controller->Ports[i]);
        if (oserr != OS_EOK) {
            WARNING(" > port %i is in disabled state", i);
            Ps2Controller->Ports[i].State = PortStateDisabled;
        }
    }
    return OS_EOK;
}

oserr_t
OnLoad(void)
{
    // Install supported protocols
    gracht_server_register_protocol(__crt_get_module_server(), &ctt_driver_server_protocol);
    gracht_server_register_protocol(__crt_get_module_server(), &ctt_input_server_protocol);

    // Allocate a new instance of the ps2-data
    Ps2Controller = (PS2Controller_t*)malloc(sizeof(PS2Controller_t));
    if (!Ps2Controller) {
        return OS_EOOM;
    }

    memset(Ps2Controller, 0, sizeof(PS2Controller_t));
    Ps2Controller->Device->Base.Id = UUID_INVALID;

    if (WaitForNetService(1000) != OS_EOK) {
        ERROR(" => Failed to start ps2 driver, as net service never became available.");
        return OS_ETIMEOUT;
    }
    return OS_EOK;
}

void
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
}

oserr_t OnEvent(struct ioset_event* event)
{
    if (event->events & IOSETSYN) {
        PS2Port_t*   port = event->data.context;
        unsigned int value;

        if (read(port->event_descriptor, &value, sizeof(unsigned int)) != sizeof(unsigned int)) {
            return OS_EUNKNOWN;
        }

        if (SIGNATURE_IS_KEYBOARD(port->Signature)) {
            PS2KeyboardInterrupt(port);
        }
        else if (SIGNATURE_IS_MOUSE(port->Signature)) {
            PS2MouseInterrupt(port);
        }
        return OS_EOK;
    }
    return OS_ENOENT;
}

void ctt_driver_register_device_invocation(struct gracht_message* message, const struct sys_device* sysDevice)
{
    Device_t*  device = from_sys_device(sysDevice);
    oserr_t    oserr;
    PS2Port_t* port;

    // First register call is the ps2-controller and all sequent calls here is ps2-devices
    // So install the contract as soon as it arrives
    if (Ps2Controller->Device->Base.Id == UUID_INVALID) {
        oserr = PS2Initialize(device);
        if (oserr != OS_EOK) {
            ERROR(" > failed to initalize ps2 driver");
        }
        return;
    }

    // Select port from device-id
    if (Ps2Controller->Ports[0].DeviceId == device->Id) {
        port = &Ps2Controller->Ports[0];
    } else if (Ps2Controller->Ports[1].DeviceId == device->Id) {
        port = &Ps2Controller->Ports[1];
    } else {
        WARNING("OnRegister unknown ps2 device");
        free(device);
        return;
    }

    // Ok .. It's a new device
    // - What kind of device?
    if (port->Signature == 0xAB41 || port->Signature == 0xABC1) { // MF2 Keyboard Translation
        oserr = PS2KeyboardInitialize(Ps2Controller, port->Index, 1);
        if (oserr != OS_EOK) {
            ERROR(" > failed to initalize ps2-keyboard");
        }
    } else if (port->Signature == 0xAB83) { // MF2 Keyboard
        oserr = PS2KeyboardInitialize(Ps2Controller, port->Index, 0);
        if (oserr != OS_EOK) {
            ERROR(" > failed to initalize ps2-keyboard");
        }
    } else if (port->Signature != 0xFFFFFFFF) {
        oserr = PS2MouseInitialize(Ps2Controller, port->Index);
        if (oserr != OS_EOK) {
            ERROR(" > failed to initalize ps2-mouse");
        }
    } else {
        WARNING("OnRegister unknown ps2 device");
    }
    free(device); // not used for ports atm
}

void ctt_driver_get_device_protocols_invocation(struct gracht_message* message, const uuid_t deviceId)
{
    // announce the protocols we support for the individual devices
    if (deviceId != Ps2Controller->Device->Base.Id) {
        ctt_driver_event_device_protocol_single(__crt_get_module_server(), message->client, deviceId,
                "input", SERVICE_CTT_INPUT_ID);
    }
}

void ctt_input_stat_invocation(struct gracht_message* message, const uuid_t deviceId)
{
    PS2Port_t* port = NULL;

    if (Ps2Controller->Ports[0].DeviceId == deviceId) {
        port = &Ps2Controller->Ports[0];
    }
    else if (Ps2Controller->Ports[1].DeviceId == deviceId) {
        port = &Ps2Controller->Ports[1];
    }

    if (port) {
        if (port->Signature == 0xAB41 || port->Signature == 0xABC1 || port->Signature == 0xAB83) {
            ctt_input_event_stats_single(__crt_get_module_server(), message->client,
                                         deviceId, CTT_INPUT_TYPE_KEYBOARD);
        }
        else {
            ctt_input_event_stats_single(__crt_get_module_server(), message->client,
                                         deviceId, CTT_INPUT_TYPE_MOUSE);
        }
    }
}

oserr_t
OnUnregister(
    _In_ Device_t* Device)
{
    oserr_t Result = OS_EUNKNOWN;
    PS2Port_t *Port;

    // Select port from device-id
    if (Ps2Controller->Ports[0].DeviceId == Device->Id) {
        Port = &Ps2Controller->Ports[0];
    }
    else if (Ps2Controller->Ports[1].DeviceId == Device->Id) {
        Port = &Ps2Controller->Ports[1];
    }
    else {
        return OS_EUNKNOWN;    // Probably the controller itself
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

void ctt_driver_ioctl_invocation(
        struct gracht_message* message,
        const uuid_t           deviceId,
        const unsigned int     request,
        const uint8_t*         out,
        const uint32_t         out_count)
{
    ctt_driver_ioctl_response(message, NULL, 0, OS_ENOTSUPPORTED);
}

// again define some stupid functions that are drawn in by libddk due to my lazy-ass approach.
void sys_device_event_protocol_device_invocation(void) {

}

void sys_device_event_device_update_invocation(void) {

}

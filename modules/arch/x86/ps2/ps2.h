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

#ifndef _DRIVER_PS2_CONTROLLER_H_
#define _DRIVER_PS2_CONTROLLER_H_

#include <ddk/busdevice.h>
#include <ddk/interrupt.h>
#include <gracht/link/socket.h>
#include <os/osdefs.h>
#include <ddk/io.h>

// Register offsets
#define PS2_REGISTER_DATA           0x00
#define PS2_REGISTER_STATUS         0x00
#define PS2_REGISTER_COMMAND        0x00

/* Some standard definitons for the PS2 controller 
 * like port count etc */
#define PS2_MAXPORTS                2
#define PS2_MAX_RETRIES             3
#define PS2_RINGBUFFER_SIZE         36      // Should be divisable between 3 and 4

/* Status definitons from reading the status
 * register in the PS2-Controller */
#define PS2_STATUS_OUTPUT_FULL      0x1
#define PS2_STATUS_INPUT_FULL       0x2

#define PS2_GET_CONFIGURATION       0x20
#define PS2_SET_CONFIGURATION       0x60
#define PS2_INTERFACETEST_PORT1     0xAB
#define PS2_INTERFACETEST_PORT2     0xA9
#define PS2_SELFTEST                0xAA
#define PS2_SELECT_PORT2            0xD4
#define PS2_ACK                     0xFA
#define PS2_RESET_PORT              0xFF
#define PS2_ENABLE_SCANNING         0xF4
#define PS2_DISABLE_SCANNING        0xF5
#define PS2_IDENTIFY_PORT           0xF2

#define PS2_SELFTEST_OK             0x55
#define PS2_INTERFACETEST_OK        0x00

#define PS2_DISABLE_PORT1           0xAD
#define PS2_ENABLE_PORT1            0xAE

#define PS2_DISABLE_PORT2           0xA7
#define PS2_ENABLE_PORT2            0xA8

/* Configuration definitions used by the above
 * commands to read/write the configuration of the PS 2 */
#define PS2_CONFIG_PORT1_IRQ        0x01
#define PS2_CONFIG_PORT2_IRQ        0x02
#define PS2_CONFIG_POST             0x04
#define PS2_CONFIG_PORT1_DISABLED   0x10
#define PS2_CONFIG_PORT2_DISABLED   0x20
#define PS2_CONFIG_TRANSLATION      0x40        /* First PS/2 port translation (1 = enabled, 0 = disabled) */

/* The IRQ lines the PS2 Controller uses, it's 
 * an ISA line so it's fixed */
#define PS2_PORT1_IRQ               0x01
#define PS2_PORT2_IRQ               0x0C

/* Command stack definitions */
#define PS2_FAILED_COMMAND          0xFF
#define PS2_RESEND_COMMAND          0xFE
#define PS2_ACK_COMMAND             0xFA

#define SIGNATURE_HAS_XLATION(sig) (sig == 0xAB41 || sig == 0xABC1)
#define SIGNATURE_IS_KEYBOARD(sig) (sig == 0xAB41 || sig == 0xABC1 || sig == 0xAB83)
#define SIGNATURE_IS_MOUSE(sig)    (sig != 0xFFFFFFFF && !SIGNATURE_IS_KEYBOARD(sig))

typedef enum PS2CommandState {
    PS2Free             = 0,
    PS2InQueue          = 1,
    PS2WaitingResponse  = 2
} PS2CommandState_t;

typedef struct PS2Command {
    volatile PS2CommandState_t State;
    volatile uint8_t           SyncObject;
    int                        RetryCount;
    uint8_t                    Command;
    uint8_t                    Buffer[2];
    uint8_t*                   Response;
} PS2Command_t;

typedef enum PS2PortState {
    PortStateDisabled,
    PortStateEnabled,
    PortStateConnected,
    PortStateActive
} PS2PortState_t;

typedef struct PS2Port {
    int               Index;
    UUId_t            DeviceId;
    DeviceInterrupt_t Interrupt;
    UUId_t            InterruptId;
    PS2Command_t      ActiveCommand;
    PS2PortState_t    State;
    unsigned int      Signature;
    gracht_client_t*  GrachtClient;
    int               event_descriptor;

    // Device state information
    uint8_t      DeviceData[6];
    uint8_t      ResponseBuffer[PS2_RINGBUFFER_SIZE];
    atomic_uint  ResponseWriteIndex;
    atomic_uint  ResponseReadIndex;
} PS2Port_t;

typedef struct PS2Controller {
    BusDevice_t Device;
    DeviceIo_t* Command;
    DeviceIo_t* Data;
    PS2Port_t   Ports[PS2_MAXPORTS];
} PS2Controller_t;

/* PS2PortInitialize
 * Initializes the given port and tries to identify the device on the port */
__EXTERN OsStatus_t
PS2PortInitialize(
    _In_ PS2Port_t* port);

/* PS2PortExecuteCommand 
 * Executes the given ps2 command, handles both retries and commands that
 * require response. */
__EXTERN OsStatus_t
PS2PortExecuteCommand(
    _In_ PS2Port_t* Port,
    _In_ uint8_t    Command,
    _In_ uint8_t*   Response);

/* PS2PortFinishCommand 
 * Finalizes the current command and executes the next command in queue (if any). */
__EXTERN OsStatus_t
PS2PortFinishCommand(
    _In_ PS2Port_t* Port);

/* PS2ReadData
 * Reads a byte from the PS2 controller data port */
__EXTERN uint8_t
PS2ReadData(
    _In_ int Dummy);

/* PS2WriteData
 * Writes a data byte to the PS2 controller data port */
__EXTERN OsStatus_t
PS2WriteData(
    _In_ uint8_t Value);

/* PS2SendCommand
 * Writes the given command to the ps2-controller */
__EXTERN void
PS2SendCommand(
    _In_ uint8_t Command);

/* PS2MouseInitialize 
 * Initializes an instance of an ps2-mouse on the given PS2-Controller port */
__EXTERN OsStatus_t
PS2MouseInitialize(
    _In_ PS2Controller_t* controller,
    _In_ int              index);

/* PS2MouseCleanup 
 * Cleans up the ps2-mouse instance on the given PS2-Controller port */
__EXTERN OsStatus_t
PS2MouseCleanup(
    _In_ PS2Controller_t* controller,
    _In_ int              index);

/* PS2MouseInterrupt 
 * Handles the ps2-mouse interrupt and processes the captured data */
__EXTERN void
PS2MouseInterrupt(
    _In_ PS2Port_t* Port);

/* PS2KeyboardInitialize 
 * Initializes an instance of an ps2-keyboard on the given PS2-Controller port */
__EXTERN OsStatus_t
PS2KeyboardInitialize(
    _In_ PS2Controller_t* Controller,
    _In_ int              port,
    _In_ int              Translation);

/* PS2KeyboardCleanup 
 * Cleans up the ps2-keyboard instance on the given PS2-Controller port */
__EXTERN OsStatus_t
PS2KeyboardCleanup(
    _In_ PS2Controller_t* controller,
    _In_ int              index);

/* PS2KeyboardInterrupt 
 * Handles the ps2-keyboard interrupt and processes the captured data */
__EXTERN void
PS2KeyboardInterrupt(
    _In_ PS2Port_t* Port);

#endif //!_DRIVER_PS2_CONTROLLER_H_

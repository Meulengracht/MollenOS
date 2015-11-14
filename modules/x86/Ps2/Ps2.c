/* MollenOS
*
* Copyright 2011 - 2014, Philip Meulengracht
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
* MollenOS X86-32 PS/2 Controller Driver
*/

/* Includes */
#include <acpi.h>
#include <Module.h>
#include <Timers.h>

/* Ps2 Drivers */
#include "Ps2.h"
#include "Keyboard\Ps2Keyboard.h"
#include "Mouse\Ps2Mouse.h"

/* Globals */
uint32_t GlbPs2Exists = 0;
uint32_t GlbPs2PortCount = 0;
uint32_t GlbPs2Port1 = 0;
uint32_t GlbPs2Port2 = 0;
Spinlock_t GlbPs2Lock;

/* Helpers */
uint8_t Ps2ReadStatus(void)
{
	return inb(X86_PS2_STATUS);
}

uint8_t Ps2ReadData(int Dummy)
{
	/* Hold response */
	uint8_t Response = 0;

	/* Get lock, but don't disable interrupts, we need them for the timeout */
	SpinlockAcquireNoInt(&GlbPs2Lock);

	if (!Dummy)
	{
		int Error = 0;

		/* Make sure output buffer is full */
		WaitForConditionWithFault(Error, (Ps2ReadStatus() & X86_PS2_STATUS_OUTPUT_FULL) == 1, 100, 10);

		if (Error)
		{
			/* Release */
			SpinlockReleaseNoInt(&GlbPs2Lock);

			/* Return err */
			return 0xFF;
		}
	}

	/* Get data */
	Response = inb(X86_PS2_DATA);

	/* Release */
	SpinlockReleaseNoInt(&GlbPs2Lock);

	/* Done */
	return Response;
}

void Ps2SendCommand(uint8_t Value)
{
	int Error = 0;

	/* Make sure input buffer is empty */
	WaitForConditionWithFault(Error, (Ps2ReadStatus() & X86_PS2_STATUS_INPUT_FULL) == 0, 100, 1);

	/* Write */
	outb(X86_PS2_COMMAND, Value);
}

int Ps2WriteData(uint8_t Value)
{
	/* If timeout happens */
	int Error = 0;

	/* Get lock, but don't disable interrupts, we need them for the timeout */
	SpinlockAcquireNoInt(&GlbPs2Lock);

	/* Make sure input buffer is empty */
	WaitForConditionWithFault(Error, (Ps2ReadStatus() & X86_PS2_STATUS_INPUT_FULL) == 0, 100, 10);

	/* Sanity */
	if (Error)
	{
		/* Release */
		SpinlockReleaseNoInt(&GlbPs2Lock);
		return Error;
	}
		
	/* Write */
	outb(X86_PS2_DATA, Value);

	/* Release */
	SpinlockReleaseNoInt(&GlbPs2Lock);

	/* Done */
	return Error;
}

/* Do Self-Test */
int Ps2SelfTest(void)
{
	/* Try 5 times */
	int i = 0;
	uint8_t Temp = 0;

	for (; i < 5; i++)
	{
		/* Write self-test command */
		Ps2SendCommand(X86_PS2_CMD_SELFTEST);

		/* Wait for response */
		Temp = Ps2ReadData(0);

		/* Did it go ok? */
		if (Temp == X86_PS2_CMD_SELFTEST_OK)
			break;
	}

	/* Yay ! */
	return (i == 5) ? 1 : 0;
}

/* Perform Interface Tests */
int Ps2InterfaceTest(int Port)
{
	/* Response */
	uint8_t Resp = 0;

	/* Send command */
	if (Port == 1)
		Ps2SendCommand(X86_PS2_CMD_IF_TEST_PORT1);
	else
		Ps2SendCommand(X86_PS2_CMD_IF_TEST_PORT2);

	/* Wait for response */
	Resp = Ps2ReadData(0);

	/* Done */
	return (Resp == X86_PS2_CMD_IF_TEST_OK) ? 0 : 1;
}

/* Reset a device */
int Ps2ResetPort(int Port)
{
	/* Response */
	uint8_t Response = 0;

	/* Select port 2 if needed */
	if (Port == 2)
		Ps2SendCommand(X86_PS2_CMD_SELECT_PORT2);

	/* Send */
	Ps2WriteData(X86_PS2_CMD_RESET_DEVICE);

	/* Read */
	Response = Ps2ReadData(0);

	/* Check */
	if (Response == 0xAA
		|| Response == 0xFA)
	{
		/* Do a few extra dummy reads */
		Response = Ps2ReadData(1);
		Response = Ps2ReadData(1);

		/* Done */
		return 0;
	}
	else
	{
		/* Failure */
		return 1;
	}
}

/* Identify a port */
uint32_t Ps2IdentifyPort(int Port)
{
	/* Response */
	uint8_t Response = 0, ResponseExtra = 0;
	uint32_t tResp = 0;
	int Error = 0;

	/* Select port 2 if needed */
	if (Port == 2)
		Ps2SendCommand(X86_PS2_CMD_SELECT_PORT2);

	/* Disable Scanning */
	Error = Ps2WriteData(0xF5);

	/* Sanity */
	if (Error)
		return 0xFFFFFFFF;

	/* Wait for response */
	Response = Ps2ReadData(0);

	/* Sanity */
	if (Response != 0xFA)
		return 0xFFFFFFFF;

	/* Select port 2 if needed */
	if (Port == 2)
		Ps2SendCommand(X86_PS2_CMD_SELECT_PORT2);

	/* Call Identify */
	Error = Ps2WriteData(0xF2);

	/* Sanity */
	if (Error)
		return 0xFFFFFFFF;

	/* Wait for response */
	Response = Ps2ReadData(0);

	/* Sanity */
	if (Response != 0xFA)
		return 0xFFFFFFFF;

	/* Get response */
GetResponse:
	Response = Ps2ReadData(0);
	if (Response == 0xFA)
		goto GetResponse;

	/* Get next byte if it exists */
	ResponseExtra = Ps2ReadData(0);
	if (ResponseExtra == 0xFF)
		ResponseExtra = 0;

	/* Gather */
	tResp = (Response << 8) | ResponseExtra;

	/* Done */
	return tResp;
}

/* Setup */
MODULES_API void ModuleInit(Addr_t *FunctionTable, void *Data)
{
	ACPI_TABLE_FADT *Fadt = NULL;
	uint8_t Temp = 0;
	int rError = 0;
	uint32_t DevId = 0;

	/* Save */
	GlbFunctionTable = FunctionTable;

	/* Cast */
	Fadt = (ACPI_TABLE_FADT*)Data;

	/* Sanity 
	 * If there is no FADT, 
	 * then the system is so old that
	 * we can pretty much assume
	 * there is a 8042 */
	if (Data != NULL)
	{
		/* Bit 1 is 8042 */
		if (!(Fadt->BootFlags & ACPI_FADT_8042))
			return;
		else
			GlbPs2Exists = 1;
	}

	/* Setup lock */
	SpinlockReset(&GlbPs2Lock);

	/* Dummy reads, empty it's buffer */
	Ps2ReadData(1);
	Ps2ReadData(1);

	/* Disable Devices */
	Ps2SendCommand(X86_PS2_CMD_DISABLE_PORT1);
	Ps2SendCommand(X86_PS2_CMD_DISABLE_PORT2);

	/* Make sure it's empty, now devices cant fill it */
	Ps2ReadData(1);

	/* Get Controller Configuration */
	Ps2SendCommand(X86_PS2_CMD_GET_CONFIG);
	Temp = Ps2ReadData(0);

	/* Port 2 Available? */
	if (Temp & 0x20)
		GlbPs2PortCount = 2;
	else
		GlbPs2PortCount = 1;

	/* Disable IRQs & Translation */
	Temp &= ~(0x3 | 0x40);

	/* Write it back */
	Ps2SendCommand(X86_PS2_CMD_SET_CONFIG);
	rError = Ps2WriteData(Temp);

	/* Perform Self Test */
	if (rError == 1 || Ps2SelfTest())
	{
		DebugPrint("Ps2 Controller failed to initialize, giving up\n");
		return;
	}

	/* Perform Interface Tests */
	if (Ps2InterfaceTest(1))
		GlbPs2Port1 = 0;
	else
		GlbPs2Port1 = 1;

	if (GlbPs2PortCount == 2)
	{
		if (Ps2InterfaceTest(2))
			GlbPs2Port2 = 0;
		else
			GlbPs2Port2 = 1;
	}

	/* Enable Devices */
	if (GlbPs2Port1)
		Ps2SendCommand(X86_PS2_CMD_ENABLE_PORT1);
	if (GlbPs2Port2)
		Ps2SendCommand(X86_PS2_CMD_ENABLE_PORT2);

	/* Get Controller Configuration again */
	Ps2SendCommand(X86_PS2_CMD_GET_CONFIG);
	Temp = Ps2ReadData(0);

	/* Lets see */
	if (GlbPs2Port1)
		Temp |= 0x1;
	if (GlbPs2Port2)
		Temp |= 0x2;

	/* Write it back */
	Ps2SendCommand(X86_PS2_CMD_SET_CONFIG);
	rError = Ps2WriteData(Temp);

	/* Sanity */
	if (rError == 1)
	{
		DebugPrint("Ps2 Controller failed to initialize, giving up\n");
		return;
	}

	/* Now, set up */
	if (GlbPs2Port1)
	{
		if (Ps2ResetPort(1))
			GlbPs2Port1 = 0;
	}
		
	if (GlbPs2Port2)
	{
		if (Ps2ResetPort(2))
			GlbPs2Port2 = 0;
	}
	
	/* Identify the different devices and load drivers */
	DevId = Ps2IdentifyPort(1);

	/* Lets Check */
	if (DevId == 0xAB41
		|| DevId == 0xABC1)
	{
		/* MF2 keyboard with translation enabled in the PS/Controller 
		 * (not possible for the second PS/2 port) */
		Ps2KeyboardInit(1, 1);
	}
	else if (DevId == 0xAB00 || DevId == 0x8300)
	{
		/* MF2 keyboard */
		Ps2KeyboardInit(1, 0);
	}
	else if (DevId != 0xFFFFFFFF)
	{
		/* Mouse */
		Ps2MouseInit(1);
	}

	/* Second Port */
	if (GlbPs2Port2)
	{
		DevId = Ps2IdentifyPort(2);

		/* Lets Check */
		if (DevId == 0xAB00 || DevId == 0x8300)
		{
			/* MF2 keyboard */
			Ps2KeyboardInit(2, 0);
		}
		else if (DevId != 0xFFFFFFFF)
		{
			/* Mouse */
			Ps2MouseInit(2);
		}
	}
}
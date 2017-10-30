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
 * MollenOS - High Performance Event Timer (HPET) Driver
 *  - Contains the implementation of the HPET driver for mollenos
 */
#define __TRACE

/* Includes
 * - System */
#include <os/driver/contracts/timer.h>
#include <os/driver/driver.h>
#include <os/utils.h>
#include "hpet.h"

/* Includes
 * - Library */
#include <stdlib.h>

/* Externs
 * Access needed for the interrupt handler in main.c */
__EXTERN
InterruptStatus_t
OnFastInterrupt(
    _In_Opt_ void *InterruptData);

/* HpRead
 * Reads the 32-bit value from the given register offset */
OsStatus_t
HpRead(
	_In_ HpController_t *Controller,
	_In_ size_t Offset,
	_Out_ reg32_t *Value)
{
	*Value = ReadIoSpace(&Controller->IoSpace, Offset, 4);
	return OsSuccess;
}

/* HpWrite
 * Writes the given 32-bit value to the given register offset */
OsStatus_t
HpWrite(
	_In_ HpController_t *Controller,
	_In_ size_t Offset,
	_In_ reg32_t Value)
{
	WriteIoSpace(&Controller->IoSpace, Offset, Value, 4);
	return OsSuccess;
}

/* HpStop
 * Stops the given controller's main counter */
OsStatus_t
HpStop(
	_In_ HpController_t *Controller)
{
	// Variables
	reg32_t Config;

	// Read, modify and write back
	HpRead(Controller, HPET_REGISTER_CONFIG, &Config);
	Config &= ~HPET_CONFIG_ENABLED;
	return HpWrite(Controller, HPET_REGISTER_CONFIG, Config);
}

/* HpStart
 * Starts the given controller's main counter */
OsStatus_t
HpStart(
	_In_ HpController_t *Controller)
{
	// Variables
	reg32_t Config;

	// Read, modify and write back
	HpRead(Controller, HPET_REGISTER_CONFIG, &Config);
	Config |= HPET_CONFIG_ENABLED;
	return HpWrite(Controller, HPET_REGISTER_CONFIG, Config);
}

/* HpReadPerformanceCounter
 * Reads the main counter register into the given structure */
OsStatus_t
HpReadPerformanceCounter(
	_In_ HpController_t *Controller,
	_Out_ LargeInteger_t *Value)
{
	// Reset value
	Value->QuadPart = 0;

	// Read lower 32 bit
	HpRead(Controller, HPET_REGISTER_MAINCOUNTER, &Value->u.LowPart);

	// Copy upper 64 bit into structure
	if (Controller->Is64Bit) {
		HpRead(Controller, HPET_REGISTER_MAINCOUNTER + 4,
			&Value->u.HighPart);
	}

	// Done
	return OsSuccess;
}

/* HpComparatorInitialize 
 * Initializes a given comparator in the HPET controller */
OsStatus_t
HpComparatorInitialize(
	_In_ HpController_t *Controller,
	_In_ int Index)
{
	// Variables
	HpTimer_t *Timer = &Controller->Timers[Index];
	reg32_t Configuration = 0;
	reg32_t InterruptMap = 0;

	// Read values
	HpRead(Controller, HPET_TIMER_CONFIG(Index), &Configuration);
	HpRead(Controller, HPET_TIMER_CONFIG(Index) + 4, &InterruptMap);

	// Setup basic information
	Timer->Present = 1;
	Timer->Irq = INTERRUPT_NONE;
	Timer->InterruptMap = InterruptMap;

	// Store some features
	if (Configuration & HPET_TIMER_CONFIG_64BITMODESUPPORT) {
		Timer->Is64Bit = 1;
	}
	if (Configuration & HPET_TIMER_CONFIG_FSBSUPPORT) {
		Timer->MsiSupport = 1;
	}
	if (Configuration & HPET_TIMER_CONFIG_PERIODICSUPPORT) {
		Timer->PeriodicSupport = 1;
	}
	
	// Process timer configuration and disable it for now
	Configuration &= ~(HPET_TIMER_CONFIG_IRQENABLED
		| HPET_TIMER_CONFIG_POLARITY | HPET_TIMER_CONFIG_FSBMODE);

	// Handle 32/64 bit
	if (!Controller->Is64Bit || !Timer->Is64Bit) {
		Configuration |= HPET_TIMER_CONFIG_32BITMODE;
	}

	// Write back configuration
	return HpWrite(Controller, HPET_TIMER_CONFIG(Index), Configuration);
}

/* HpComparatorStart
 * Starts a given comparator in the HPET controller with the
 * given frequency (hz) */
OsStatus_t
HpComparatorStart(
	_In_ HpController_t *Controller,
	_In_ int Index,
	_In_ uint64_t Frequency,
	_In_ int Periodic)
{
	// Variables
	HpTimer_t *Timer = &Controller->Timers[Index];
	MCoreInterrupt_t Interrupt;
	LargeInteger_t Now;
	uint64_t Delta;
	reg32_t TempValue;
	int i, j;

	// Stop main timer
	HpStop(Controller);

	// Calculate the delta
	HpReadPerformanceCounter(Controller, &Now);
	Delta = (uint64_t)Controller->Frequency.QuadPart / Frequency;
	Now.QuadPart += Delta;

	// Allocate interrupt for timer?
	if (Timer->Irq == INTERRUPT_NONE) {
		// Initialize interrupt structure
		Interrupt.Line = INTERRUPT_NONE;
		Interrupt.Pin = INTERRUPT_NONE;
		Interrupt.AcpiConform = 0;

		// Set handler and data
		Interrupt.Data = Controller;
        Interrupt.FastHandler = OnFastInterrupt;

		// From the interrupt map, calculate possible int's
		for (i = 0, j = 0; i < 32; i++) {
			if (Timer->InterruptMap & (1 << i)) {
				Interrupt.Vectors[j++] = i;
				if (j == INTERRUPT_MAXVECTORS) {
					break;
				}
			}
		}

		// Place an end marker
		if (j != INTERRUPT_MAXVECTORS) {
			Interrupt.Vectors[j] = INTERRUPT_NONE;
		}

		// Handle MSI interrupts > normal
		if (Timer->MsiSupport) {
			Timer->Interrupt =
				RegisterInterruptSource(&Interrupt, INTERRUPT_MSI);
			Timer->MsiAddress = (reg32_t)Interrupt.MsiAddress;
			Timer->MsiValue = (reg32_t)Interrupt.MsiValue;
		}
		else {
			Timer->Interrupt =
				RegisterInterruptSource(&Interrupt, INTERRUPT_VECTOR);
			Timer->Irq = Interrupt.Line;
		}
	}
	
	// Process configuration
	HpRead(Controller, HPET_TIMER_CONFIG(Index), &TempValue);
	TempValue |= HPET_TIMER_CONFIG_IRQENABLED;
	
	// Set interrupt vector
	// MSI must be set to edge-triggered
	if (Timer->MsiSupport) {
		TempValue |= HPET_TIMER_CONFIG_FSBMODE;

		// Update FSB registers
		HpWrite(Controller, HPET_TIMER_FSB(Index), Timer->MsiValue);
		HpWrite(Controller, HPET_TIMER_FSB(Index) + 4, Timer->MsiAddress);
	}
	else {
		TempValue |= HPET_TIMER_CONFIG_IRQ(Timer->Irq);
		if (Timer->Irq > 15) {
			TempValue |= HPET_TIMER_CONFIG_POLARITY;
		}
	}

	// Set some extra bits if periodic
	if (Timer->PeriodicSupport && Periodic) {
		TempValue |= HPET_TIMER_CONFIG_PERIODIC;
		TempValue |= HPET_TIMER_CONFIG_SET_CMP_VALUE;
	}

	// Update configuration and comparator
	HpWrite(Controller, HPET_TIMER_CONFIG(Index), TempValue);
	HpWrite(Controller, HPET_TIMER_COMPARATOR(Index), Now.u.LowPart);
	if (!(TempValue & HPET_TIMER_CONFIG_32BITMODE)) {
		HpWrite(Controller, HPET_TIMER_COMPARATOR(Index) + 4, Now.u.HighPart);
	}

	// Write delta if periodic
	if (Timer->PeriodicSupport && Periodic) {
		HpWrite(Controller, HPET_TIMER_COMPARATOR(Index), LODWORD(Delta));
	}

	// Set enabled
	Timer->Enabled = 1;

	// Clear interrupt
	HpWrite(Controller, HPET_REGISTER_INTSTATUS, (1 << Index));

	// Start main timer
	return HpStart(Controller);
}

/* HpControllerCreate 
 * Creates a new controller from the given device descriptor */
HpController_t*
HpControllerCreate(
	_In_ MCoreDevice_t *Device,
	_In_ ACPI_TABLE_HPET *Table)
{
	// Variables
	HpController_t *Controller = NULL;
	int Legacy = 0, FoundPeriodic = 0;
	reg32_t TempValue;
	int i, NumTimers;

	// Trace
	TRACE("HpControllerCreate(Address 0x%x, Sequence %u)",
		(uintptr_t)(Table->Address.Address & __MASK),
		Table->Sequence);

	// Allocate a new controller instance
	Controller = (HpController_t*)malloc(sizeof(HpController_t));
	memset(Controller, 0, sizeof(HpController_t));
	memcpy(&Controller->Device, Device, sizeof(MCoreDevice_t));

	// Initialize io-space
	Controller->IoSpace.PhysicalBase = 
		(uintptr_t)(Table->Address.Address & __MASK);
	Controller->IoSpace.Size = HPET_IOSPACE_LENGTH;

	// Determine type
	if (Table->Address.SpaceId == 0) {
		Controller->IoSpace.Type = IO_SPACE_MMIO;
	}
	else {
		Controller->IoSpace.Type = IO_SPACE_IO;
	}

	// Initialize the contracts
	InitializeContract(&Controller->ContractTimer, Device->Id, 1,
		ContractTimer, "HPET Timer Controller");
	InitializeContract(&Controller->ContractPerformance, Device->Id, 1,
		ContractTimerPerformance, "HPET Performance Controller");

	// Create the io-space
	if (CreateIoSpace(&Controller->IoSpace) != OsSuccess
		&& AcquireIoSpace(&Controller->IoSpace) != OsSuccess) {
		ERROR("Failed to acquire HPET io-space");
		free(Controller);
		return NULL;
	}

	// Store some data
	Controller->TickMinimum = Table->MinimumTick;
	HpRead(Controller, HPET_REGISTER_CAPABILITIES + 4, &Controller->Period);

	// Trace
	TRACE("Hpet (Minimum Tick 0x%x, Period 0x%x)",
		Controller->TickMinimum, Controller->Period);

	// AMD SB700 Systems initialise HPET on first register access,
	// wait for it to setup HPET, its config register reads 0xFFFFFFFF meanwhile
	for (i = 0; i < 10000; i++) {
		HpRead(Controller, HPET_REGISTER_CONFIG, &TempValue);
		if (TempValue != 0xFFFFFFFF) {
			break;
		}
	}

	// Did system fail to initialize
	if (TempValue == 0xFFFFFFFF 
		|| (Controller->Period == 0)
		|| (Controller->Period > HPET_MAXPERIOD)) {
		ERROR("Failed to initialize HPET (AMD SB700) or period is invalid.");
		ReleaseIoSpace(&Controller->IoSpace);
		DestroyIoSpace(Controller->IoSpace.Id);
		free(Controller);
		return NULL;
	}

	// Calculate the frequency
	Controller->Frequency.QuadPart = (int64_t)
		(uint64_t)(FSEC_PER_SEC / (uint64_t)Controller->Period);

	// Process the capabilities
	HpRead(Controller, HPET_REGISTER_CAPABILITIES, &TempValue);
	Controller->Is64Bit = (TempValue & HPET_64BITSUPPORT) ? 1 : 0;
	Legacy = (TempValue & HPET_LEGACYMODESUPPORT) ? 1 : 0;
	NumTimers = (int)HPET_TIMERCOUNT(TempValue);

	// Trace
	TRACE("Hpet (Caps 0x%x, Timers 0x%x, Frequency 0x%x)", 
		TempValue, NumTimers, Controller->Frequency.u.LowPart);

	// Sanitize the number of timers, must be above 0
	if (NumTimers == 0 || NumTimers > HPET_MAXTIMERCOUNT) {
		ERROR("There was no timers present in HPET");
		ReleaseIoSpace(&Controller->IoSpace);
		DestroyIoSpace(Controller->IoSpace.Id);
		free(Controller);
		return NULL;
	}

	// Halt the main timer and start configuring it
	// We want to disable the legacy if its supported and enabled
	HpRead(Controller, HPET_REGISTER_CONFIG, &TempValue);

	// Disable legacy and counter
	TempValue &= ~(HPET_CONFIG_ENABLED);
	if (Legacy != 0) {
		TempValue &= ~(HPET_CONFIG_LEGACY);
	}

	// Update config and reset main counter
	HpWrite(Controller, HPET_REGISTER_CONFIG, TempValue);
	HpWrite(Controller, HPET_REGISTER_MAINCOUNTER, 0);
	HpWrite(Controller, HPET_REGISTER_MAINCOUNTER + 4, 0);

	// Loop through all comparators and configurate them
	for (i = 0; i < NumTimers; i++) {
		if (HpComparatorInitialize(Controller, i) == OsError) {
			ERROR("HPET Failed to initialize comparator %i", i);
			Controller->Timers[i].Present = 0;
		}
	}

	// Register the contract before setting up rest
	if (RegisterContract(&Controller->ContractTimer) != OsSuccess
		&& RegisterContract(&Controller->ContractPerformance) != OsSuccess) {
		ERROR("Failed to register HPET Contracts");
		ReleaseIoSpace(&Controller->IoSpace);
		DestroyIoSpace(Controller->IoSpace.Id);
		free(Controller);
		return NULL;
	}

	// Iterate and find periodic timer
	// and install that one as system timer
	for (i = 0; i < NumTimers; i++) {
		if (Controller->Timers[i].Present
			&& Controller->Timers[i].PeriodicSupport) {
			if (HpComparatorStart(Controller, i, 1000, 1) != OsSuccess) {
				ERROR("HPET Failed to initialize periodic timer %i", i);
			}
			else {
				if (RegisterSystemTimer(Controller->Timers[i].Interrupt, 1000) != OsSuccess) {
					ERROR("HPET Failed register timer %i as the system timer", i);
				}
				else {
					Controller->Timers[i].SystemTimer = 1;
					FoundPeriodic = 1;
					break;
				}
			}
		}
	}

	// If we didn't find periodic, use the first present
	// timer as one-shot and reinit it every interrupt
	if (!FoundPeriodic) {
		ERROR("HPET (No periodic timer present!)");
	}

	// Success!
	return Controller;
}

/* HpControllerDestroy
 * Destroys an already registered controller and all its 
 * registers sub-timers */
OsStatus_t
HpControllerDestroy(
	_In_ HpController_t *Controller)
{
	// Todo
	_CRT_UNUSED(Controller);
	return OsSuccess;
}

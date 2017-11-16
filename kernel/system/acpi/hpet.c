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
#define __MODULE "HPET"
#define __TRACE

/* Includes
 * - System */
#include <interrupts.h>
#include <timers.h>
#include <debug.h>
#include <hpet.h>
#include <heap.h>

/* Includes
 * - Library */
#include <stdlib.h>

/* Globals
 * We keep a controller-structure as static, as there can only be
 * one controller present. */
static HpController_t HpetController;

/* HpRead
 * Reads the 32-bit value from the given register offset */
OsStatus_t
HpRead(
	_In_ size_t Offset,
	_Out_ reg32_t *Value)
{
	*Value = *((reg32_t*)(HpetController.BaseAddress + Offset));
	return OsSuccess;
}

/* HpWrite
 * Writes the given 32-bit value to the given register offset */
OsStatus_t
HpWrite(
	_In_ size_t Offset,
	_In_ reg32_t Value)
{
	*((reg32_t*)(HpetController.BaseAddress + Offset)) = Value;
	return OsSuccess;
}

/* HpStop
 * Stops the given controller's main counter */
OsStatus_t
HpStop(void)
{
	// Variables
	reg32_t Config;

	// Read, modify and write back
	HpRead(HPET_REGISTER_CONFIG, &Config);
	Config &= ~HPET_CONFIG_ENABLED;
	return HpWrite(HPET_REGISTER_CONFIG, Config);
}

/* HpStart
 * Starts the given controller's main counter */
OsStatus_t
HpStart(void)
{
	// Variables
	reg32_t Config;

	// Read, modify and write back
	HpRead(HPET_REGISTER_CONFIG, &Config);
	Config |= HPET_CONFIG_ENABLED;
	return HpWrite(HPET_REGISTER_CONFIG, Config);
}

/* HpGetTicks
 * Retrieves the number of ticks done by the PIT. */
clock_t
HpGetTicks(void)
{
    return HpetController.Clock;
}

/* HpReadFrequency
 * Reads the main frequency value into the given structure */
void
HpReadFrequency(
	_Out_ LargeInteger_t *Value)
{
    memcpy(Value, &HpetController.Frequency, sizeof(LargeInteger_t));
}

/* HpReadPerformance
 * Reads the main counter register into the given structure */
void
HpReadPerformance(
	_Out_ LargeInteger_t *Value)
{
	// Reset value
	Value->QuadPart = 0;

	// Read lower 32 bit
	HpRead(HPET_REGISTER_MAINCOUNTER, &Value->u.LowPart);
	if (HpetController.Is64Bit) {
		HpRead(HPET_REGISTER_MAINCOUNTER + 4, &Value->u.HighPart);
	}
}

/* HpInterrupt
 * HPET Interrupt handler */
InterruptStatus_t
HpInterrupt(
    _In_Opt_ void *InterruptData)
{
    // Variables
	reg32_t InterruptStatus = 0;
	int i                   = 0;

	// Initiate values
	HpRead(HPET_REGISTER_INTSTATUS, &InterruptStatus);

	// Trace
	TRACE("Interrupt - Status 0x%x", InterruptStatus);

	// Was the interrupt even from this controller?
	if (!InterruptStatus) {
		return InterruptNotHandled;
	}

	// Iterate the port-map and check if the interrupt
	// came from that timer
	for (i = 0; i < HPET_MAXTIMERCOUNT; i++) {
		if (InterruptStatus & (1 << i)
			&& HpetController.Timers[i].Enabled) {
			if (HpetController.Timers[i].SystemTimer) {
				HpetController.Clock++;
                TimersInterrupt(HpetController.Timers[i].Interrupt);
			}
			if (!HpetController.Timers[i].PeriodicSupport) {
				// Non periodic timer fired, what now?
				WARNING("HPET::NON-PERIODIC TIMER FIRED");
			}
		}
	}

	// Write clear interrupt register and return
	HpWrite(HPET_REGISTER_INTSTATUS, InterruptStatus);
	return InterruptHandled;
}

/* HpComparatorInitialize 
 * Initializes a given comparator in the HPET controller */
OsStatus_t
HpComparatorInitialize(
	_In_ int Index)
{
	// Variables
	HpTimer_t *Timer        = &HpetController.Timers[Index];
	reg32_t Configuration   = 0;
	reg32_t InterruptMap    = 0;

	// Read values
	HpRead(HPET_TIMER_CONFIG(Index), &Configuration);
	HpRead(HPET_TIMER_CONFIG(Index) + 4, &InterruptMap);

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
	if (!HpetController.Is64Bit || !Timer->Is64Bit) {
		Configuration |= HPET_TIMER_CONFIG_32BITMODE;
	}

	// Write back configuration
	return HpWrite(HPET_TIMER_CONFIG(Index), Configuration);
}

/* HpComparatorStart
 * Starts a given comparator in the HPET controller with the
 * given frequency (hz) */
OsStatus_t
HpComparatorStart(
	_In_ int Index,
	_In_ uint64_t Frequency,
	_In_ int Periodic)
{
	// Variables
    MCoreInterrupt_t HpetInterrupt;
	LargeInteger_t Now;
	HpTimer_t *Timer    = &HpetController.Timers[Index];
	uint64_t Delta      = 0;
	reg32_t TempValue   = 0;
	int i, j;

	// Stop main timer
	HpStop();

	// Calculate the delta
	HpReadPerformance(&Now);
	Delta = (uint64_t)HpetController.Frequency.QuadPart / Frequency;
	Now.QuadPart += Delta;

	// Allocate interrupt for timer?
	if (Timer->Irq == INTERRUPT_NONE) {
        // Setup interrupt
        memset(&HpetInterrupt, 0, sizeof(MCoreInterrupt_t));
        HpetInterrupt.Data = Timer;
        HpetInterrupt.Line = INTERRUPT_NONE;
        HpetInterrupt.Pin = INTERRUPT_NONE;
        HpetInterrupt.FastHandler = HpInterrupt;

		// From the interrupt map, calculate possible int's
		for (i = 0, j = 0; i < 32; i++) {
			if (Timer->InterruptMap & (1 << i)) {
				HpetInterrupt.Vectors[j++] = i;
				if (j == INTERRUPT_MAXVECTORS) {
					break;
				}
			}
		}

		// Place an end marker
		if (j != INTERRUPT_MAXVECTORS) {
			HpetInterrupt.Vectors[j] = INTERRUPT_NONE;
		}

		// Handle MSI interrupts > normal
		if (Timer->MsiSupport) {
			Timer->Interrupt = 
                InterruptRegister(&HpetInterrupt, INTERRUPT_MSI | INTERRUPT_KERNEL);
			Timer->MsiAddress = (reg32_t)HpetInterrupt.MsiAddress;
			Timer->MsiValue = (reg32_t)HpetInterrupt.MsiValue;
		}
		else {
			Timer->Interrupt =
				InterruptRegister(&HpetInterrupt, INTERRUPT_VECTOR | INTERRUPT_KERNEL);
			Timer->Irq = HpetInterrupt.Line;
		}
	}
	
	// Process configuration
	HpRead(HPET_TIMER_CONFIG(Index), &TempValue);
	TempValue |= HPET_TIMER_CONFIG_IRQENABLED;
	
	// Set interrupt vector
	// MSI must be set to edge-triggered
	if (Timer->MsiSupport) {
		TempValue |= HPET_TIMER_CONFIG_FSBMODE;

		// Update FSB registers
		HpWrite(HPET_TIMER_FSB(Index), Timer->MsiValue);
		HpWrite(HPET_TIMER_FSB(Index) + 4, Timer->MsiAddress);
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
	HpWrite(HPET_TIMER_CONFIG(Index), TempValue);
	HpWrite(HPET_TIMER_COMPARATOR(Index), Now.u.LowPart);
	if (!(TempValue & HPET_TIMER_CONFIG_32BITMODE)) {
		HpWrite(HPET_TIMER_COMPARATOR(Index) + 4, Now.u.HighPart);
	}

	// Write delta if periodic
	if (Timer->PeriodicSupport && Periodic) {
		HpWrite(HPET_TIMER_COMPARATOR(Index), LODWORD(Delta));
	}

	// Set enabled
	Timer->Enabled = 1;

	// Clear interrupt
	HpWrite(HPET_REGISTER_INTSTATUS, (1 << Index));

	// Start main timer
	return HpStart();
}

/* HpInitialize
 * Initializes the ACPI hpet timer from the hpet table. */
ACPI_STATUS
HpInitialize(
	_In_ ACPI_TABLE_HPET *Table)
{
	// Variables
    MCoreTimePerformanceOps_t PerformanceOps = { 
        HpReadFrequency, HpReadPerformance, NULL
    };
	int Legacy = 0, FoundPeriodic = 0;
	reg32_t TempValue;
	int i, NumTimers;

	// Trace
	TRACE("HpInitialize(Address 0x%x, Sequence %u)",
		(uintptr_t)(Table->Address.Address & __MASK),
		Table->Sequence);

	// Initialize the structure
	memset(&HpetController, 0, sizeof(HpController_t));

	// Initialize io-space
	HpetController.BaseAddress = 
		(uintptr_t)(Table->Address.Address & __MASK);

	// Store some data
	HpetController.TickMinimum = Table->MinimumTick;
	HpRead(HPET_REGISTER_CAPABILITIES + 4, &HpetController.Period);

	// Trace
	TRACE("Minimum Tick 0x%x, Period 0x%x",
		HpetController.TickMinimum, HpetController.Period);

	// AMD SB700 Systems initialise HPET on first register access,
	// wait for it to setup HPET, its config register reads 0xFFFFFFFF meanwhile
	for (i = 0; i < 10000; i++) {
		HpRead(HPET_REGISTER_CONFIG, &TempValue);
		if (TempValue != 0xFFFFFFFF) {
			break;
		}
	}

	// Did system fail to initialize
	if (TempValue == 0xFFFFFFFF 
		|| (HpetController.Period == 0)
		|| (HpetController.Period > HPET_MAXPERIOD)) {
		ERROR("Failed to initialize HPET (AMD SB700) or period is invalid.");
		return AE_ERROR;
	}

	// Calculate the frequency
	HpetController.Frequency.QuadPart = (int64_t)
		(uint64_t)(FSEC_PER_SEC / (uint64_t)HpetController.Period);

	// Process the capabilities
	HpRead(HPET_REGISTER_CAPABILITIES, &TempValue);
	HpetController.Is64Bit = (TempValue & HPET_64BITSUPPORT) ? 1 : 0;
	Legacy = (TempValue & HPET_LEGACYMODESUPPORT) ? 1 : 0;
	NumTimers = (int)HPET_TIMERCOUNT(TempValue);

	// Trace
	TRACE("Caps 0x%x, Timers 0x%x, Frequency 0x%x", 
		TempValue, NumTimers, HpetController.Frequency.u.LowPart);

	// Sanitize the number of timers, must be above 0
	if (NumTimers == 0 || NumTimers > HPET_MAXTIMERCOUNT) {
		ERROR("There was no timers present in HPET");
		return AE_ERROR;
	}

	// Halt the main timer and start configuring it
	// We want to disable the legacy if its supported and enabled
	HpRead(HPET_REGISTER_CONFIG, &TempValue);

	// Disable legacy and counter
	TempValue &= ~(HPET_CONFIG_ENABLED);
	if (Legacy != 0) {
		TempValue &= ~(HPET_CONFIG_LEGACY);
	}

	// Update config and reset main counter
	HpWrite(HPET_REGISTER_CONFIG, TempValue);
	HpWrite(HPET_REGISTER_MAINCOUNTER, 0);
	HpWrite(HPET_REGISTER_MAINCOUNTER + 4, 0);

	// Loop through all comparators and configurate them
	for (i = 0; i < NumTimers; i++) {
		if (HpComparatorInitialize(i) == OsError) {
			ERROR("HPET Failed to initialize comparator %i", i);
			HpetController.Timers[i].Present = 0;
		}
	}

	// Iterate and find periodic timer
	// and install that one as system timer
	for (i = 0; i < NumTimers; i++) {
		if (HpetController.Timers[i].Present
			&& HpetController.Timers[i].PeriodicSupport) {
			if (HpComparatorStart(i, 1000, 1) != OsSuccess) {
				ERROR("Failed to initialize periodic timer %i", i);
			}
			else {
				if (TimersRegisterSystemTimer(HpetController.Timers[i].Interrupt, 
                        1000, HpGetTicks) != OsSuccess) {
					ERROR("Failed register timer %i as the system timer", i);
				}
				else {
					HpetController.Timers[i].SystemTimer = 1;
					FoundPeriodic = 1;
					break;
				}
			}
		}
	}

    // Register high performance timers
    if (TimersRegisterPerformanceTimer(&PerformanceOps) != OsSuccess) {
        ERROR("Failed to register the performance handlers");
    }    

	// If we didn't find periodic, use the first present
	// timer as one-shot and reinit it every interrupt
	if (!FoundPeriodic) {
		WARNING("No periodic timer present!");
	}

	// Success!
	return AE_OK;
}

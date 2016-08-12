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
* MollenOS X86 Devices Interfaces
*
*/

/* Includes */
#include <AcpiInterface.h>
#include <DeviceManager.h>
#include <Modules/ModuleManager.h>
#include "../Arch.h"
#include <Memory.h>
#include <Pci.h>
#include <Heap.h>
#include <Timers.h>
#include <Log.h>

/* C-Library */
#include <ds/list.h>

/* Definitions */
#define DEVICES_CMOS			0x00000000
#define DEVICES_PS2				0x00000010
#define DEVICES_PIT				0x00000018
#define DEVICES_RTC				0x00000020

/* Initialises all available timers in system */
void DevicesInitTimers(void)
{
	/* Vars */
	MCoreModule_t *Module = NULL;
	ACPI_TABLE_HEADER *Header = NULL;

	/* Information */
	LogInformation("TIMR", "Initializing System Timers");

	/* Step 1. Load the CMOS Clock */
	Module = ModuleFindGeneric(DEVICEMANAGER_LEGACY_CLASS, DEVICES_CMOS);

	/* Do we have the driver? */
	if (Module != NULL)
		ModuleLoad(Module, &AcpiGbl_FADT.Century);

	/* Step 2. Try to setup HPET 
	 * I'd rather ignore the rest */
	if (ACPI_SUCCESS(AcpiGetTable(ACPI_SIG_HPET, 0, &Header)))
	{
		/* There is hope, we have Hpet header */
		Module = ModuleFindGeneric(DEVICEMANAGER_LEGACY_CLASS, DEVICEMANAGER_ACPI_HPET);

		/* Do we have the driver? */
		if (Module != NULL)
		{
			/* Cross fingers for the Hpet driver */
			if (ModuleLoad(Module, (void*)Header) == ModuleOk)
				return;
		}
	}

	/* Damn.. 
	 * Step 3. Initialize the PIT */
	Module = ModuleFindGeneric(DEVICEMANAGER_LEGACY_CLASS, DEVICES_PIT);

	/* Do we have the driver? */
	if (Module != NULL)
	{
		/* Great, load driver */
		if (ModuleLoad(Module, NULL) == ModuleOk)
			return;
	}
		
	/* Wtf? No PIT? 
	 * Step 4. Last resort to the rtc-clock */
	Module = ModuleFindGeneric(DEVICEMANAGER_LEGACY_CLASS, DEVICES_RTC);

	/* Do we have the driver? */
	if (Module != NULL)
		ModuleLoad(Module, NULL);

	/* Step 3. Install PS2 if present */
	if (AcpiAvailable() == ACPI_NOT_AVAILABLE
		|| (AcpiGbl_FADT.BootFlags & ACPI_FADT_8042)) 
	{
		/* PS2 */
		Module = ModuleFindGeneric(DEVICEMANAGER_LEGACY_CLASS, DEVICES_PS2);

		/* Do we have the driver? */
		if (Module != NULL)
			ModuleLoad(Module, NULL);
	}
}


/* Backup Timer, Should always be provided */
extern void rdtsc(uint64_t *Value);
extern x86CpuObject_t GlbBootCpuInfo;

void DelayMs(uint32_t MilliSeconds)
{
	/* Keep value in this */
	uint64_t Counter = 0;
	volatile uint64_t TimeOut = 0;

	/* Sanity */
	if (!(GlbBootCpuInfo.EdxFeatures & CPUID_FEAT_EDX_TSC))
	{
		LogFatal("TIMR", "DelayMs() was called, but no TSC support in CPU.");
		Idle();
	}

	/* Use rdtsc for this */
	rdtsc(&Counter);
	TimeOut = Counter + (uint64_t)(MilliSeconds * 100000);

	while (Counter < TimeOut) { rdtsc(&Counter); }
}
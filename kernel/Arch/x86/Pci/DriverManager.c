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
* MollenOS X86-32 Driver Manager
* Version 1. PCI Support Only (No PCI Express)
*/

/* Includes */
#include <Arch.h>
#include <Mutex.h>
#include <assert.h>
#include <acpi.h>
#include <Pci.h>
#include <List.h>
#include <Heap.h>
#include <stddef.h>
#include <stdio.h>
#include <limits.h>

/* Globals */
volatile uint32_t GlbBusCounter = 0;

/* This enumerates EHCI controllers and makes sure all routing goes to
 * their companion controllers */
void DriverDisableEhci(void *Data, int n)
{
	PciDevice_t *Driver = (PciDevice_t*)Data;
	list_t *SubBusList;

	/* Unused */
	_CRT_UNUSED(n);

	/* Check type */
	switch (Driver->Type)
	{
	case X86_PCI_TYPE_BRIDGE:
	{
		/* Sanity */
		if (Driver->Children != NULL)
		{
			/* Get bus list */
			SubBusList = (list_t*)Driver->Children;

			/* Install drivers on that bus */
			list_execute_all(SubBusList, DriverDisableEhci);
		}
		
	} break;

	case X86_PCI_TYPE_DEVICE:
	{
		/* Get driver */

		/* Serial Bus Comms */
		if (Driver->Header->Class == 0x0C)
		{
			/* Usb? */
			if (Driver->Header->Subclass == 0x03)
			{
				/* Controller Type? */

				/* UHCI -> 0. OHCI -> 0x10. EHCI -> 0x20. xHCI -> 0x30 */
				if (Driver->Header->Interface == 0x20)
				{
					/* Initialise Controller */
					EhciInit(Driver);
				}
			}
		}

	} break;

	default:
		break;
	}
}

/* This installs a driver for each device present (if we have a driver!) */
void DriverSetupCallback(void *Data, int n)
{
	PciDevice_t *driver = (PciDevice_t*)Data;
	list_t *sub_bus; 

	/* We dont really use 'n' */
	_CRT_UNUSED(n);

	switch (driver->Type)
	{
		case X86_PCI_TYPE_BRIDGE:
		{
			/* Get bus list */
			sub_bus = (list_t*)driver->Children;

			/* Install drivers on that bus */
			list_execute_all(sub_bus, DriverSetupCallback);

		} break;

		case X86_PCI_TYPE_DEVICE:
		{
			/* Serial Bus Comms */
			if (driver->Header->Class == 0x0C)
			{
				/* Usb? */
				if (driver->Header->Subclass == 0x03)
				{
					/* Controller Type? */

					/* UHCI -> 0. OHCI -> 0x10. EHCI -> 0x20. xHCI -> 0x30 */

					if (driver->Header->Interface == 0x0)
					{
						/* Initialise Controller */
						//uhci_init(driver);
					}
					else if (driver->Header->Interface == 0x10)
					{
						/* Initialise Controller */
						OhciInit(driver);
					}
				}
			}

		} break;

		default:
			break;
	}
}

/* Initialises all available drivers in system */
void DriverManagerInit(void *Args)
{
	/* Unused */
	_CRT_UNUSED(Args);

	/* Start out by enumerating devices */
#ifdef X86_PCI_DIAGNOSE
	printf("    * Enumerating PCI Space\n");
#endif
	PciAcpiEnumerate();

	/* Debug */
#ifdef X86_PCI_DIAGNOSE
	printf("    * Device Enumeration Done!\n");
#endif

	/* Special Step for EHCI Controllers
	* This is untill I know OHCI and UHCI works perfectly! */
	list_execute_all(GlbPciDevices, DriverDisableEhci);

	/* Now, for each driver we have available install it */
	list_execute_all(GlbPciDevices, DriverSetupCallback);

	/* Setup Fixed Devices */
	Ps2Init();
}
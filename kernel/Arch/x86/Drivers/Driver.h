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
* MollenOS X86-32 Driver PCI Strings
*/

#ifndef _X86_DRIVER_H_
#define _X86_DRIVER_H_

/* Includes */
#include <Pci.h>
#include <stddef.h>

/* Structures */
typedef struct _DeviceDriver
{
	/* Name */
	char *DeviceName;

	/* Information */
	uint32_t Status;
	uint32_t IrqLine;

	/* Pci Interface */
	PciDevice_t *PciDevice;

	/* Driver Data */
	void *DriverData;

} DeviceDriver_t;

/* Definitions */


#endif
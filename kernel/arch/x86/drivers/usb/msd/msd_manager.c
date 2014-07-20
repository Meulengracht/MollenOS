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
* MollenOS X86-32 USB Core MSD Driver
*/

/* Includes */
#include <arch.h>
#include <drivers\usb\usb.h>
#include <drivers\usb\msd\msd_manager.h>
#include <semaphore.h>
#include <heap.h>
#include <list.h>
#include <stdio.h>
#include <string.h>

/* Initialise Driver for a MSD */
void usb_msd_initialise(usb_hc_device_t *device, uint32_t iface)
{
	_CRT_UNUSED(device);
	_CRT_UNUSED(iface);
}
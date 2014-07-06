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
* MollenOS X86-32 USB Core Driver
*/

#ifndef X86_USB_H_
#define X86_USB_H_

/* Includes */
#include <crtdefs.h>
#include <stdint.h>

/* Definitions */

/* Structures */



/* The Abstract Controller */
typedef struct _usb_hc
{
	/* Controller Type */
	uint32_t type;

	/* Controller Data */
	void *hc;

	/* Ports */

} usb_hc_t;

/* Prototypes */
_CRT_EXTERN void usb_register_controller(usb_hc_t *controller);

#endif // !X86_USB_H_

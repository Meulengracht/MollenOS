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
 * MollenOS Console Interface (Boot)
 * - Contains the implementation and interface of the output environment
 *   for the operating system.
 */

#ifndef __CONSOLE_INTERFACE_H__
#define __CONSOLE_INTERFACE_H__

#include <os/osdefs.h>
#include <ddk/contracts/video.h>

/* InitializeSerialOutput (@arch)
 * Initializes the serial output for the operating system. This is the output that is
 * always active if a serial port is present. */
KERNELAPI OsStatus_t KERNELABI
InitializeSerialOutput(void);

/* InitializeFramebufferOutput (@arch)
 * Initializes the video framebuffer of the operating system. This enables visual rendering
 * of the operating system debug console. */
KERNELAPI OsStatus_t KERNELABI
InitializeFramebufferOutput(void);

/* InitializeConsole
 * Initializes the output environment. This enables either visual representation
 * and debugging of the kernel and enables a serial debugger. */
KERNELAPI OsStatus_t KERNELABI
InitializeConsole(void);

/* VideoQuery
 * Renders a character with default colors
 * at the current terminal position */
KERNELAPI OsStatus_t KERNELABI
VideoQuery(
	_Out_ VideoDescriptor_t *Descriptor);

#endif //!__CONSOLE_INTERFACE_H__

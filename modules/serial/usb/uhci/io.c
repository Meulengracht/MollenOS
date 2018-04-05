/* MollenOS
 *
 * Copyright 2011, Philip Meulengracht
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
 * MollenOS MCore - Universal Host Controller Interface Driver
 * Todo:
 * Power Management
 */
//#define __TRACE

/* Includes
 * - System */
#include <os/utils.h>
#include "uhci.h"

/* UhciRead16
 * Reads a 2-byte value from the control-space of the controller */
uint16_t
UhciRead16(
	_In_ UhciController_t*  Controller, 
	_In_ uint16_t           Register)
{
	// Wrapper for reading the io-space
	return (uint16_t)ReadIoSpace(Controller->Base.IoBase, Register, 2);
}

/* UhciRead32
 * Reads a 4-byte value from the control-space of the controller */
uint32_t
UhciRead32(
	_In_ UhciController_t*  Controller, 
	_In_ uint16_t           Register)
{
	// Wrapper for reading the io-space
	return (uint32_t)ReadIoSpace(Controller->Base.IoBase, Register, 4);
}

/* UhciWrite8
 * Writes a single byte value to the control-space of the controller */
void
UhciWrite8(
	_In_ UhciController_t*  Controller, 
	_In_ uint16_t           Register, 
	_In_ uint8_t            Value)
{
	// Wrapper for writing to the io-space
	WriteIoSpace(Controller->Base.IoBase, Register, Value, 1);
}

/* UhciWrite16
 * Writes a 2-byte value to the control-space of the controller */
void
UhciWrite16(
	_In_ UhciController_t*  Controller, 
	_In_ uint16_t           Register, 
	_In_ uint16_t           Value)
{ 
	// Wrapper for writing to the io-space
	WriteIoSpace(Controller->Base.IoBase, Register, Value, 2);
}

/* UhciWrite32
 * Writes a 4-byte value to the control-space of the controller */
void 
UhciWrite32(
	_In_ UhciController_t*  Controller, 
	_In_ uint16_t           Register, 
	_In_ uint32_t           Value)
{
	// Wrapper for writing to the io-space
	WriteIoSpace(Controller->Base.IoBase, Register, Value, 4);
}

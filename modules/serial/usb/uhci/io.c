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
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * Universal Host Controller Interface Driver
 * Todo:
 * Power Management
 */
//#define __TRACE

#include <ddk/utils.h>
#include "uhci.h"

uint8_t
UhciRead8(
	_In_ UhciController_t*  Controller, 
	_In_ uint16_t           Register)
{
	return LOBYTE(ReadDeviceIo(Controller->Base.IoBase, Register, 1));
}

uint16_t
UhciRead16(
	_In_ UhciController_t*  Controller, 
	_In_ uint16_t           Register)
{
	return LOWORD(ReadDeviceIo(Controller->Base.IoBase, Register, 2));
}

uint32_t
UhciRead32(
	_In_ UhciController_t*  Controller, 
	_In_ uint16_t           Register)
{
	return LODWORD(ReadDeviceIo(Controller->Base.IoBase, Register, 4));
}

void
UhciWrite8(
	_In_ UhciController_t*  Controller, 
	_In_ uint16_t           Register, 
	_In_ uint8_t            Value)
{
	WriteDeviceIo(Controller->Base.IoBase, Register, Value, 1);
}

void
UhciWrite16(
	_In_ UhciController_t*  Controller, 
	_In_ uint16_t           Register, 
	_In_ uint16_t           Value)
{ 
	WriteDeviceIo(Controller->Base.IoBase, Register, Value, 2);
}

void 
UhciWrite32(
	_In_ UhciController_t*  Controller, 
	_In_ uint16_t           Register, 
	_In_ uint32_t           Value)
{
	WriteDeviceIo(Controller->Base.IoBase, Register, Value, 4);
}

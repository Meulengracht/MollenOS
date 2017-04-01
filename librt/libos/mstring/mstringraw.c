/* MollenOS
*
* Copyright 2011 - 2016, Philip Meulengracht
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
* MollenOS MCore - String Format
*/

/* Includes
* - System */
#include "MStringPrivate.h"

/* Returns the raw data pointer 
 * of the given MString, which can be used for
 * usage, not recommended to edit data */
const char *MStringRaw(MString_t *String)
{
	/* Cast it to a const pointer before 
	 * passing it on for security reasons */
	return (const char*)String->Data;
}
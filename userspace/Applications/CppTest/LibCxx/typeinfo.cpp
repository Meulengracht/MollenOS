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
* MollenOS Visual C++ Implementation
* These stubs don't need to do anything (those private functions are never
* called). They need to be in cpprt, though, in order to have the vtable
* and generated destructor thunks available to programs
*/

/* Includes */
#include <typeinfo>
#include <stdlib.h>

/* Empty cctor */
type_info::type_info(const type_info &) {
}

/* Empty dctor */
type_info::~type_info() {
	/* Cleanup the name */
	if (this->_name) {
		free(this->_name);
	}
}

/* Empty assignor */
type_info &type_info::operator=(const type_info &) {
	return *this;
}

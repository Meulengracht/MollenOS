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

#include "mvcxx.h"
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <intrin.h>

/* Name unmangling, VC++ */
extern "C" char* __cdecl __unDName(char *,
	const char*, int, malloc_func_t, free_func_t, unsigned short int);

/* Empty cctor */
type_info::type_info(const type_info &) {
	this->_name = NULL;
}

/* Empty dctor */
type_info::~type_info() {
	/* Cleanup the name */
	if (this->_name) {
		free(this->_name);
	}
}

/* Get name function */
const char *type_info::name() const 
{
	/* Allocate name if it's not already */
	if (!this->_name)
	{
		/* Create and set the demangled name */
		/* Note: mangled name in type_info struct always starts with a '.', while
		* it isn't valid for mangled name.
		* Is this '.' really part of the mangled name, or has it some other meaning ?
		*/
		char* name = __unDName(0, this->_mangled + 1, 0,
			malloc, free, UNDNAME_NO_ARGUMENTS | UNDNAME_32_BIT_DECODE);
		if (name)
		{
			size_t len = strlen(name);

			/* It seems _unDName may leave blanks at the end of the demangled name */
			while (len && name[--len] == ' ')
				name[len] = '\0';

			if (_InterlockedCompareExchangePointer((void**)&this->_name, name, NULL))
			{
				/* Another thread set this member since we checked above - use it */
				free(name);
			}
		}
	}

	/* Done! */
	return this->_name;
}

/* Get raw name function */
const char *type_info::raw_name() const {
	return (const char*)&this->_mangled[0];
}

/* Equal operator */
int type_info::operator==(const type_info &rhs) const {
	return !strcmp(this->_mangled + 1, rhs.raw_name() + 1);
}

/* Not equal operator */
int type_info::operator!=(const type_info &rhs) const {
	return !!strcmp(this->_mangled + 1, rhs.raw_name() + 1);
}

/* Before function */
int type_info::before(const type_info &rhs) const {
	return (strcmp(this->_mangled + 1, rhs.raw_name() + 1) < 0);
}

/* Empty assignor */
type_info &type_info::operator=(const type_info &) {
	return *this;
}

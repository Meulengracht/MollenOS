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
*/

/* Includes */
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include "mvcxx.h"

/* C++ */
#include <exception>

/* Globals */
extern const vTablePtr cxx_exception_vtable;
extern const vTablePtr cxx_bad_typeid_vtable;
extern const vTablePtr cxx_bad_cast_vtable;
extern const vTablePtr cxx___non_rtti_object_vtable;
extern const vTablePtr cxx_type_info_vtable;

/* Internal common ctor for exception */
static void _InternalExceptionConstruct(InternalException_t *_this, const char **ExcName, int NoAlloc)
{
	/* Access it's virtual table */
	_this->vTable = &cxx_exception_vtable;

	/* Is it a noalloc instance? */
	if (NoAlloc != 0) {
		_this->ExcName = (char*)(*ExcName);
		_this->FreeName = false;
		return;
	}

	/* Is there a name available 
	 * for this exception? */
	if (*ExcName)
	{
		/* Make a copy of the name 
		 * but first we need to get size of 
		 * the needed str buffer */
		size_t Length = strlen(*ExcName) + 1;

		/* Allocate */
		_this->ExcName = (char*)malloc(Length);

		/* Copy the data over */
		memcpy(_this->ExcName, *ExcName, Length);

		/* Yea.. we need to free */
		_this->FreeName = true;
	}
	else {
		_this->ExcName = NULL;
		_this->FreeName = false;
	}
}

/* The common constructor for
 * exceptions */
exception::exception() {
	/* Empty description string */
	const char* EmptyStr = NULL;

	/* Deep call to shared constructor */
	_InternalExceptionConstruct((InternalException_t*)this, &EmptyStr, 0);
}

/* The common constructor for
 * exceptions */
exception::exception(const char * const &ExceptionName) {
	/* Deep call to shared constructor */
	_InternalExceptionConstruct((InternalException_t*)this, (const char **)ExceptionName, 0);
}

/* The common constructor for
 * exceptions */
exception::exception(const char * const &ExceptionName, int NoAlloc) {
	/* Deep call to shared constructor */
	_InternalExceptionConstruct((InternalException_t*)this, (const char **)ExceptionName, 1);
}

/* The common destructor for 
 * exception classes */
exception::~exception() 
{
	/* Start by casting it so we can access 
	 * the virtual table */
	InternalException_t *_IntExc = (InternalException_t*)this;

	/* Update virtual table */
	_IntExc->vTable = &cxx_exception_vtable;

	/* Free resources if neccessary */
	if (_IntExc->FreeName) {
		free(_IntExc->ExcName);
	}
}

/* The get description of the exception 
 * method, now the description can be 
 * null, and here we want to return a default 
 * description */
const char *exception::what() const {
	/* Sanity */
	if (this->_name == NULL) {
		return "Unknown Exception";
	}
	else
		return this->_name;
}
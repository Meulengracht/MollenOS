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
#include "mvcxx.h"
#include <stdlib.h>
#include <new>

/* Forward declarations */
void* operator new (std::size_t);
void* operator new[](std::size_t);

/* Prototype */
const std::nothrow_t std::nothrow;

/* Standard new operator 
 * function */
void *operator new(size_t size)
{
	/* Deep call */
	return malloc(size);
}

/* Standard new array operator 
 * function */
 void *operator new[](size_t size)
{
	/* Deep call */
	return malloc(size);
}

/* New operator */
void* operator new (std::size_t size, const std::nothrow_t& nothrow_constant) throw()
{
	/* Not used */
	_CRT_UNUSED(nothrow_constant);

	/* Encapsulate the new and catch 
	 * exceptions so others don't have to */
	try
	{
		return operator new (size);
	}
	catch (std::bad_alloc)
	{
		/* Our failsafe */
		return NULL;
	}
}

/* New array operator */
void* operator new[](std::size_t size, const std::nothrow_t& nothrow_constant) throw()
{
	/* Not used */
	_CRT_UNUSED(nothrow_constant);

	/* Encapsulate the new and catch 
	 * exceptions so others don't have to */
	try
	{
		return operator new[](size);
	}
	catch (std::bad_alloc)
	{
		/* Our failsafe */
		return NULL;
	}
}

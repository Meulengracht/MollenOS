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
#include <typeinfo>

#include "mvcxx.h"
#include <stdlib.h>
#include <stddef.h>
#include <string.h>

/* OS - Includes */
#include <os/Thread.h>

/* Extenrns */
extern "C" void CxxThrowException(InternalException_t *ExcObject, const CxxExceptionType_t *ExcType);
extern "C" void *CxxGetThisPointer(const CxxThisPtrOffsets_t *Offsets, void *Object);

/* Rtti Base Descriptor */
typedef struct _rtti_base_descriptor
{
	const type_info *type_descriptor;
	int num_base_classes;
	CxxThisPtrOffsets_t offsets;    /* offsets for computing the this pointer */
	unsigned int attributes;
} rtti_base_descriptor;

/* Rtti Base Array */
typedef struct _rtti_base_array
{
	const rtti_base_descriptor *bases[3]; /* First element is the class itself */
} rtti_base_array;

/* Rtti Object Hierarchy */
typedef struct _rtti_object_hierarchy
{
	unsigned int signature;
	unsigned int attributes;
	int array_len; /* Size of the array pointed to by 'base_classes' */
	const rtti_base_array *base_classes;
} rtti_object_hierarchy;

/* Rtti Object Locator */
typedef struct _rtti_object_locator
{
	unsigned int signature;
	int base_class_offset;
	unsigned int flags;
	const type_info *type_descriptor;
	const rtti_object_hierarchy *type_hierarchy;
} rtti_object_locator;

/* Get the vtable pointer for a C++ object */
static inline const vTablePtr *CxxGetVTable(void *Object) 
{
	/* Transform pointer */
	return *(const vTablePtr**)Object;
}

/* Get the object locator structure for a 
 * specific Cxx Object */
static inline const rtti_object_locator *CxxGetObjectLocator(void *Object)
{
	/* Lookup */
	const vTablePtr *vTable = CxxGetVTable(Object);

	/* Cast pointer */
	return (const rtti_object_locator *)vTable[-1];
}

/******************************************************************
*		__RTtypeid (MSVCRT.@)
*
* Retrieve the Run Time Type Information (RTTI) for a C++ object.
*
* PARAMS
*  Object [I] C++ object to get type information for.
*
* RETURNS
*  Success: A type_info object describing cppobj.
*  Failure: If the object to be cast has no RTTI, a __non_rtti_object
*           exception is thrown. If cppobj is NULL, a bad_typeid exception
*           is thrown. In either case, this function does not return.
*
* NOTES
*  This function is usually called by compiler generated code as a result
*  of using one of the C++ dynamic cast statements.
*/
extern "C" void* __cdecl __RTtypeid(void *Object) throw()
{
	/* Variables */
	const type_info *RetInfo;

	/* Validate pointer */
	if (!Object) {
		/* Throw up */
		throw bad_typeid("Attempted a typeid of NULL pointer!");
	}

	/* Try to locate type information for
	 * the given object */
	__try
	{
		const rtti_object_locator *obj_locator = CxxGetObjectLocator(Object);
		RetInfo = obj_locator->type_descriptor;
	}
	__except (((PEXCEPTION_RECORD)TLSGetCurrent()->ExceptionRecord)->ExceptionCode
		== EXCEPTION_ACCESS_VIOLATION ?
			EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH)
	{
		/* Allocate a new non_rtti object */
		throw __non_rtti_object("Bad read pointer - no RTTI data!");
	}

	/* Done! */
	return (void*)RetInfo;
}

/******************************************************************
*		__RTDynamicCast (MSVCRT.@)
*
* Dynamically cast a C++ object to one of its base classes.
*
* PARAMS
*  cppobj   [I] Any C++ object to cast
*  unknown  [I] Reserved, set to 0
*  src      [I] type_info object describing cppobj
*  dst      [I] type_info object describing the base class to cast to
*  do_throw [I] TRUE = throw an exception if the cast fails, FALSE = don't
*
* RETURNS
*  Success: The address of cppobj, cast to the object described by dst.
*  Failure: NULL, If the object to be cast has no RTTI, or dst is not a
*           valid cast for cppobj. If do_throw is TRUE, a bad_cast exception
*           is thrown and this function does not return.
*
* NOTES
*  This function is usually called by compiler generated code as a result
*  of using one of the C++ dynamic cast statements.
*/
void* __cdecl __RTDynamicCast(void *Object, int unknown,
	type_info *src, type_info *dst, int do_throw) throw()
{
	void *RetInfo;

	if (!Object) 
		return NULL;

	/* Unused */
	_CRT_UNUSED(src);
	_CRT_UNUSED(unknown);

	/* To cast an object at runtime:
	* 1.Find out the true type of the object from the typeinfo at vtable[-1]
	* 2.Search for the destination type in the class hierarchy
	* 3.If destination type is found, return base object address + dest offset
	*   Otherwise, fail the cast
	*
	* FIXME: the unknown parameter doesn't seem to be used for anything
	*/
	__try
	{
		int i;
		const rtti_object_locator *ObjLocator = CxxGetObjectLocator(Object);
		const rtti_object_hierarchy *obj_bases = ObjLocator->type_hierarchy;
		const rtti_base_descriptor * const* base_desc = obj_bases->base_classes->bases;

		RetInfo = NULL;
		for (i = 0; i < obj_bases->array_len; i++)
		{
			const type_info *typ = base_desc[i]->type_descriptor;

			if (!strcmp(typ->raw_name(), dst->raw_name()))
			{
				/* compute the correct this pointer for that base class */
				void *this_ptr = (char *)Object - ObjLocator->base_class_offset;
				RetInfo = CxxGetThisPointer(&base_desc[i]->offsets, this_ptr);
				break;
			}
		}
		/* VC++ sets do_throw to 1 when the result of a dynamic_cast is assigned
		* to a reference, since references cannot be NULL.
		*/
		if (!RetInfo && do_throw) {
			//throw bad_cast((const char * const *)"Bad dynamic_cast!");
		}
	}
	__except (((PEXCEPTION_RECORD)TLSGetCurrent()->ExceptionRecord)->ExceptionCode
		== EXCEPTION_ACCESS_VIOLATION ?
			EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH)
	{
		throw __non_rtti_object("Access violation - no RTTI data!");
	}

	/* Done! */
	return RetInfo;
}


/******************************************************************
*		__RTCastToVoid (MSVCRT.@)
*
* Dynamically cast a C++ object to a void*.
*
* PARAMS
*  cppobj [I] The C++ object to cast
*
* RETURNS
*  Success: The base address of the object as a void*.
*  Failure: NULL, if cppobj is NULL or has no RTTI.
*
* NOTES
*  This function is usually called by compiler generated code as a result
*  of using one of the C++ dynamic cast statements.
*/
extern "C" void* __cdecl __RTCastToVoid(void *Object) throw()
{
	void *RetInfo;

	/* Sanitize param */
	if (!Object) 
		return NULL;

	__try
	{
		/* Get object locator */
		const rtti_object_locator *ObjLocator = CxxGetObjectLocator(Object);

		/* Get offset */
		RetInfo = (char *)Object - ObjLocator->base_class_offset;
	}
	__except (((PEXCEPTION_RECORD)TLSGetCurrent()->ExceptionRecord)->ExceptionCode
		== EXCEPTION_ACCESS_VIOLATION ? 
			EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH)
	{
		/* Allocate a new non_rtti object */
		throw __non_rtti_object("Access violation - no RTTI data!");
	}

	/* Done! */
	return RetInfo;
}
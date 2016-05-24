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
* MollenOS - Generic Data Structures (Shared)
*/

#ifndef _DATASTRUCTURES_H_
#define _DATASTRUCTURES_H_

/* Includes */
#include <crtdefs.h>
#include <stdint.h>

/*******************************
 *    Data Structures          *
 *******************************/

/* The definition of a key
 * in generic data-structures
 * this can be values or data */
typedef union _DataKey
{
	/* Integer Value */
	int Value;

	/* Data Pointer */
	void *Pointer;

	/* String */
	char *String;

} DataKey_t;

/* This enumeration denotes
 * the kind of key that is
 * to be interpreted by the
 * data-structure */
typedef enum _KeyType
{
	KeyInteger,
	KeyPointer,
	KeyString

} KeyType_t;

/* Data-structure locking 
 * primitive definition */
#ifdef LIBC_KERNEL
#include <Arch.h>
#else
#include <os/Thread.h>
#endif

/*******************************
 *         Prototypes          *
 *******************************/

/* This is used by data-structures 
 * to allocate memory, since it will 
 * be different for kernel/clib */
_CRT_EXTERN void *dsalloc(size_t size);
_CRT_EXTERN void dsfree(void *p);

/* Helper Function 
 * Matches two keys based on the key type 
 * returns 0 if they are equal, or -1 if not */
_CRT_EXTERN int dsmatchkey(KeyType_t KeyType, DataKey_t Key1, DataKey_t Key2);

#endif //!_DATASTRUCTURES_H_
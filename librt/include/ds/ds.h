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
 * MollenOS - Generic Data Structures (Shared)
 */

#ifndef _DATASTRUCTURES_H_
#define _DATASTRUCTURES_H_

/* Includes
 * - Library */
#include <os/osdefs.h>

/* The definition of a key
 * in generic data-structures this can be values or data */
typedef union _DataKey {
    int           Value;
    void         *Pointer;
    char         *String;
} DataKey_t;

/* This enumeration denotes
 * the kind of key that is to be interpreted by the
 * data-structure */
typedef enum _KeyType {
    KeyInteger,
    KeyPointer,
    KeyString
} KeyType_t;

/* dsalloc
 * Seperate portable memory allocator for data-structures */
CRTDECL(
void*,
dsalloc(
    _In_ size_t size));

/* dsfree
 * Seperate portable memory freeing for data-structures */
CRTDECL(
void,
dsfree(
    _In_ void *p));

/* Helper Function 
 * Matches two keys based on the key type 
 * returns 0 if they are equal, or -1 if not */
CRTDECL(
int,
dsmatchkey(
    _In_ KeyType_t KeyType, 
    _In_ DataKey_t Key1, 
    _In_ DataKey_t Key2));

/* Helper Function
 * Used by sorting, it compares to values
 * and returns 1 if 1 > 2, 0 if 1 == 2 and
 * -1 if 2 > 1 */
CRTDECL(
int,
dssortkey(
    _In_ KeyType_t KeyType, 
    _In_ DataKey_t Key1, 
    _In_ DataKey_t Key2));

#endif //!_DATASTRUCTURES_H_

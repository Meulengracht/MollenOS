/* MollenOS
*
* Copyright 2011 - 2015, Philip Meulengracht
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

#ifndef _MCORE_STRING_H_
#define _MCORE_STRING_H_

/* Includes */
#include <stdint.h>
#include <crtdefs.h>

/* Definitions */
#define MSTRING_NOT_FOUND		-1

/* String Types */
typedef enum _MStringType
{
	StrASCII,
	StrUTF8,
	StrUTF16,
	StrUTF32
} MStringType_t;

/* Structures */
typedef struct _MString
{
	/* String Data
	 * As UTF8 */
	void *Data;
	uint32_t DataLength;

} MString_t;

/* Prototypes */
_CRT_EXTERN MString_t *MStringCreate(void *Data, MStringType_t DataType);
_CRT_EXTERN void MStringDestroy(MString_t *String);

/* Substring */
_CRT_EXTERN int MStringFind(MString_t *String, uint32_t Character);
_CRT_EXTERN MString_t *MStringSubString(MString_t *String, int Index, int Length);

/* Utilities */
_CRT_EXTERN uint32_t MStringLength(MString_t *String);
_CRT_EXTERN uint32_t MStringCompare(MString_t *String1, MString_t *String2, uint32_t IgnoreCase);
_CRT_EXTERN void MStringToASCII(MString_t *String, void *Buffer);
_CRT_EXTERN void MStringPrint(MString_t *String);

/* Casing */
_CRT_EXTERN void MStringUpperCase(MString_t *String);
_CRT_EXTERN MString_t *MStringUpperCaseCopy(MString_t *String);

_CRT_EXTERN void MStringLowerCase(MString_t *String);
_CRT_EXTERN MString_t *MStringLowerCaseCopy(MString_t *String);

#endif //!_MCORE_STRING_H_
/* MollenOS
*
* Copyright 2011 - 2014, Philip Meulengracht
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
* MollenOS Synchronization
* Critical Sections
*/

#ifndef _MCORE_CRITICAL_SECTION_
#define _MCORE_CRITICAL_SECTION_

/* Includes */
#include <Arch.h>
#include <Threading.h>
#include <crtdefs.h>
#include <stdint.h>

/* Structures */
typedef struct _CriticalSection
{
	/* Owner */
	TId_t Owner;

	/* References */
	size_t References;

	/* Spinlock */
	Spinlock_t Lock;

} CriticalSection_t;

/* Prototypes */
_CRT_EXPORT CriticalSection_t *CriticalSectionCreate(void);
_CRT_EXPORT void CriticalSectionDestroy(CriticalSection_t *Section);

_CRT_EXPORT void CriticalSectionConstruct(CriticalSection_t *Section);
_CRT_EXPORT void CriticalSectionEnter(CriticalSection_t *Section);
_CRT_EXPORT void CriticalSectionLeave(CriticalSection_t *Section);

#endif
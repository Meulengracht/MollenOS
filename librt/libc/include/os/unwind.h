/* MollenOS
 *
 * Copyright 2019, Philip Meulengracht
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
 * Process Service Definitions & Structures
 * - This header describes the base process-structure, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#ifndef __V_UNWIND_H__
#define __V_UNWIND_H__

#include <os/osdefs.h>

typedef struct UnwindSection {
    void*  ModuleBase;
    void*  UnwindSectionBase;
    size_t UnwindSectionLength;
} UnwindSection_t;

_CODE_BEGIN
/**
 * UnwindGetSection
 * * Retrieve the unwind section for the module containing the given address
 */
CRTDECL(OsStatus_t,
UnwindGetSection(
	_In_ void*            MemoryAddress,
	_In_ UnwindSection_t* Section));

_CODE_END

#endif //!__V_UNWIND_H__

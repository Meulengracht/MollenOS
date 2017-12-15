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
 * MollenOS C Environment - Shared Routines
 */

/* Includes
 * - Library */
#include <os/osdefs.h>

typedef void(*_PVFV)(void);
typedef int(*_PIFV)(void);
typedef void(*_PVFI)(int);

/* CRT Segments
 * - COFF Linker segments for initializers/finalizers 
 *   I = C, C = C++, P = Pre-Terminators, T = Terminators. */
#pragma data_seg(".CRT$XIA")
__declspec(allocate(".CRT$XIA")) _PIFV __xi_a[] = { 0 };
#pragma data_seg(".CRT$XIZ")
__declspec(allocate(".CRT$XIZ")) _PIFV __xi_z[] = { 0 };
#pragma data_seg(".CRT$XCA")
__declspec(allocate(".CRT$XCA")) _PVFV __xc_a[] = { 0 };
#pragma data_seg(".CRT$XCZ")
__declspec(allocate(".CRT$XCZ")) _PVFV __xc_z[] = { 0 };
#pragma data_seg(".CRT$XPA")
__declspec(allocate(".CRT$XPA")) _PVFV __xp_a[] = { 0 };
#pragma data_seg(".CRT$XPZ")
__declspec(allocate(".CRT$XPZ")) _PVFV __xp_z[] = { 0 };
#pragma data_seg(".CRT$XTA")
__declspec(allocate(".CRT$XTA")) _PVFV __xt_a[] = { 0 };
#pragma data_seg(".CRT$XTZ")
__declspec(allocate(".CRT$XTZ")) _PVFV __xt_z[] = { 0 };
#pragma data_seg()
#pragma comment(linker, "/merge:.CRT=.data")

/* Externs 
 * - Access to lib-c initializers */
CRTDECL(void, __CrtCallInitializers(_PVFV *pfbegin, _PVFV *pfend));
CRTDECL(int, __CrtCallInitializersEx(_PIFV *pfbegin, _PIFV *pfend));

/* Globals
 * - Global exported shared variables */
void *__dso_handle = &__dso_handle;

void __CrtCxxInitialize(void) {
	__CrtCallInitializers(__xc_a, __xc_z);
	__CrtCallInitializersEx(__xi_a, __xi_z);
}

void __CrtCxxFinalize(void) {
	__CrtCallInitializers(__xp_a, __xp_z);
	__CrtCallInitializersEx(__xt_a, __xt_z);
}

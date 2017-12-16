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
 * MollenOS C Library - Entry Points
 */

/* Includes 
 * - Library */
#include <os/osdefs.h>

/* Extern
 * - C/C++ Initialization
 * - C/C++ Cleanup */
#ifdef __clang__
__EXTERN void __CrtCxxInitialize(void);
CRTDECL(void, __cxa_finalize(void *Dso));
__EXTERN void *__dso_handle;
#else
__EXTERN void __CppInit(void);
__EXTERN void __CppFinit(void);
#endif
__EXTERN void dllmain(int action);

/* __CrtLibraryEntry
 * Library crt initialization routine. This runs
 * available C++ constructors/destructors. */
void
__CrtLibraryEntry(int Action)
{
	switch (Action) {
        case DLL_ACTION_INITIALIZE: {
            // Module has been attached to system.
#ifdef __clang__
            __CrtCxxInitialize();
#else
            __CppInit();
#endif
            dllmain(DLL_ACTION_INITIALIZE);
        } break;
        case DLL_ACTION_FINALIZE: {
            // Module is being unloaded
            dllmain(DLL_ACTION_FINALIZE);
#ifdef __clang__
            __cxa_finalize(__dso_handle);
#else
            __CppFinit();
#endif
        } break;
    }
}

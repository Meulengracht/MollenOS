/* MollenOS
 *
 * Copyright 2011, Philip Meulengracht
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
 * Library CRT Setup
 */

#include <os/osdefs.h>

extern void __cxa_module_global_init(void);
extern void __cxa_module_global_finit(void);
extern void __cxa_module_tls_thread_init(void);
extern void __cxa_module_tls_thread_finit(void);
CRTDECL(void, __cxa_tls_thread_cleanup(void *Dso));
CRTDECL(void, __cxa_tls_module_cleanup(void *Dso));
extern void dllmain(int action);
extern void *__dso_handle;

void
__CrtLibraryEntry(int Action)
{
	switch (Action) {
        case DLL_ACTION_INITIALIZE: {
            // Module has been attached to system.
            __cxa_module_global_init();
            dllmain(DLL_ACTION_INITIALIZE);
        } break;
        case DLL_ACTION_FINALIZE: {
            // Module is being unloaded
            dllmain(DLL_ACTION_FINALIZE);
            __cxa_tls_thread_cleanup(__dso_handle);
            __cxa_tls_module_cleanup(__dso_handle);
            __cxa_module_global_finit();
        } break;
        case DLL_ACTION_THREADATTACH: {
            __cxa_module_tls_thread_init();
            dllmain(DLL_ACTION_THREADATTACH);
        } break;
        case DLL_ACTION_THREADDETACH: {
            dllmain(DLL_ACTION_THREADDETACH);
            __cxa_tls_thread_cleanup(__dso_handle);
            __cxa_module_tls_thread_finit();
        } break;
    }
}

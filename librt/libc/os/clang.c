/* MollenOS
 *
 * Copyright 2017, Philip Meulengracht
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
 * CRT Functions 
 */
//#define __TRACE

#include <internal/_syscalls.h>
#include <internal/_utils.h>
#include <os/osdefs.h>
#include <os/spinlock.h>
#include <ddk/services/process.h>
#include <ddk/utils.h>
#include <threads.h>
#include <stdlib.h>
#include <assert.h>
#include <math.h>
#include "../threads/tls.h"

#ifndef __INTERNAL_FUNC_DEFINED
#define __INTERNAL_FUNC_DEFINED
typedef void(*_PVFV)(void);
typedef int(*_PIFV)(void);
typedef void(*_PVFI)(int);
typedef void(*_PVTLS)(void*, unsigned long, void*);
#endif

extern OsStatus_t CRTHIDE ProcessGetLibraryEntryPoints(Handle_t LibraryList[PROCESS_MAXMODULES]);
extern void               StdioCleanup(void);
extern void               tls_atexit(_In_ thrd_t thr, _In_ void (*Function)(void*), _In_ void* Argument, _In_ void* DsoHandle);
extern void               tls_atexit_quick(_In_ thrd_t thr, _In_ void (*Function)(void*), _In_ void* Argument, _In_ void* DsoHandle);

static int      CleanupPerformed               = 0;
static Handle_t ModuleList[PROCESS_MAXMODULES] = { 0 };
static void   (*__cxa_primary_cleanup)(void);
static void   (*__cxa_primary_tls_thread_init)(void);
static void   (*__cxa_primary_tls_thread_finit)(void);

CRTDECL(void,
__cxa_callinitializers(_PVFV *pfbegin, _PVFV *pfend))
{
    TRACE("__cxa_callinitializers()");
    while (pfbegin < pfend) {
        if (*pfbegin != NULL) {
            TRACE(" > invoking 0x%" PRIxIN, *pfbegin);
            (**pfbegin)();
        }
        ++pfbegin;
    }
}

CRTDECL(int,
__cxa_callinitializers_ex(_PIFV *pfbegin, _PIFV *pfend))
{
    TRACE("__cxa_callinitializers_ex()");
    int ret = 0;
    while (pfbegin < pfend  && ret == 0) {
        if (*pfbegin != NULL) {
            TRACE(" > invoking 0x%" PRIxIN, *pfbegin);
            ret = (**pfbegin)();
        }
        ++pfbegin;
    }
    return ret;
}

CRTDECL(void,
__cxa_callinitializers_tls(
    _In_ _PVTLS*        pfbegin,
    _In_ _PVTLS*        pfend,
    _In_ void*          dso_handle,
    _In_ unsigned long  reason))
{
    TRACE("__cxa_callinitializers_tls()");
    while (pfbegin < pfend) {
        if (*pfbegin != NULL) {
            TRACE(" > invoking 0x%" PRIxIN, *pfbegin);
            (**pfbegin)(dso_handle, reason, NULL);
        }
        ++pfbegin;
    }
}

void CRTHIDE
__cxa_exithandlers(
    _In_ int Status,
    _In_ int Quick,
    _In_ int DoAtExit,
    _In_ int CleanupCrt)
{
    TRACE("__cxa_exithandlers()");
    // Avoid recursive calls or anything to this
    if (CleanupPerformed != 0) {
        return;
    }
    CleanupPerformed = 1;

    // Run dynamic crt for primary application
    if (CleanupCrt != 0) {
        if (!Quick) { tls_cleanup(thrd_current(), NULL, Status); tls_cleanup(UUID_INVALID, NULL, Status); }
        else        { tls_cleanup_quick(thrd_current(), NULL, Status); tls_cleanup_quick(UUID_INVALID, NULL, Status); }
    }

    // Run dynamic/static for all modules
    if (!Quick) {
        // Run at-exit lists for all the modules
        if (DoAtExit != 0) {
            for (int i = 0; i < PROCESS_MAXMODULES; i++) {
                if (ModuleList[i] == NULL) {
                    break;
                }
                ((void (*)(int))ModuleList[i])(DLL_ACTION_FINALIZE);
            }
            // Cleanup primary app
            __cxa_primary_cleanup();
        }
    }

    // Cleanup crt if asked
    if (CleanupCrt != 0) {
        StdioCleanup();
    }
    tls_destroy(tls_current());
}

/* __cxa_getentrypoints
 * Retrieves a list of entry points for loaded libraries. */
OsStatus_t __cxa_getentrypoints(Handle_t LibraryList[PROCESS_MAXMODULES])
{
    TRACE("__cxa_getentrypoints()");
    if (IsProcessModule()) {
        return Syscall_ModuleGetModuleEntryPoints(LibraryList);
    }
    return ProcessGetLibraryEntryPoints(LibraryList);
}

/* __cxa_runinitializers 
 * C++ Initializes library C++ runtime for all loaded modules */
CRTDECL(void, __cxa_runinitializers(
    _In_ void (*module_init)(void), 
    _In_ void (*module_cleanup)(void),
    _In_ void (*module_thread_init)(void),
    _In_ void (*module_thread_finit)(void)))
{
    TRACE("__cxa_runinitializers()");
    fpreset();
    if (__cxa_getentrypoints(ModuleList) == OsSuccess) {
        for (int i = 0; i < PROCESS_MAXMODULES; i++) {
            if (ModuleList[i] == NULL) {
                break;
            }
            ((void (*)(int))ModuleList[i])(DLL_ACTION_INITIALIZE);
        }
    }

    // Run callers initializer
    module_init();
    __cxa_primary_cleanup          = module_cleanup;
    __cxa_primary_tls_thread_init  = module_thread_init;
    __cxa_primary_tls_thread_finit = module_thread_finit;
}

/* __cxa_threadinitialize
 * Initializes thread storage runtime for all loaded modules */
CRTDECL(void, __cxa_threadinitialize(void))
{
    TRACE("__cxa_threadinitialize()");
    fpreset();
    if ((ModuleList[0] != 0) || (__cxa_getentrypoints(ModuleList) == OsSuccess)) {
        for (int i = 0; i < PROCESS_MAXMODULES; i++) {
            if (ModuleList[i] == NULL) {
                break;
            }
            ((void (*)(int))ModuleList[i])(DLL_ACTION_THREADATTACH);
        }
    }
    __cxa_primary_tls_thread_init();
}

/* __cxa_threadfinalize
 * Finalizes thread storage runtime for all loaded modules */
CRTDECL(void, __cxa_threadfinalize(void))
{
    TRACE("__cxa_threadfinalize()");
    if ((ModuleList[0] != 0) || (__cxa_getentrypoints(ModuleList) == OsSuccess)) {
        for (int i = 0; i < PROCESS_MAXMODULES; i++) {
            if (ModuleList[i] == NULL) {
                break;
            }
            ((void (*)(int))ModuleList[i])(DLL_ACTION_THREADDETACH);
        }
    }
    __cxa_primary_tls_thread_finit();
}

CRTDECL(void, __cxa_tls_thread_cleanup(void *Dso))
{
    TRACE("__cxa_tls_thread_cleanup()");
    tls_cleanup(thrd_current(), Dso, 0);
}

CRTDECL(void, __cxa_tls_module_cleanup(void *Dso))
{
    TRACE("__cxa_tls_module_cleanup()");
    tls_cleanup(UUID_INVALID, Dso, 0);
}

/* __cxa_finalize
 * C++ At-Exit implementation for dynamic cleanup of registered handlers */
CRTDECL(void, __cxa_finalize(void *Dso))
{
    __cxa_tls_thread_cleanup(Dso);
    __cxa_tls_module_cleanup(Dso);
}

/* __cxa_atexit/__cxa_at_quick_exit 
 * C++ At-Exit implementation for registering of exit-handlers. */
CRTDECL(int, __cxa_atexit(void (*Function)(void*), void *Argument, void *Dso)) {
    TRACE("__cxa_atexit()");
    tls_atexit(UUID_INVALID, Function, Argument, Dso);
    return 0;
}
CRTDECL(int, __cxa_at_quick_exit(void (*Function)(void*), void *Dso)) {
    TRACE("__cxa_at_quick_exit()");
    tls_atexit_quick(UUID_INVALID, Function, NULL, Dso);
    return 0;
}

/* __cxa_thread_atexit_impl/__cxa_thread_at_quick_exit_impl
 * C++ At-Exit implementation for thread specific cleanup. */
CRTDECL(int, __cxa_thread_atexit_impl(void (*dtor)(void*), void* arg, void* dso_symbol)) {
    TRACE("__cxa_thread_atexit_impl()");
    tls_atexit(thrd_current(), dtor, arg, dso_symbol);
    return 0;
}

CRTDECL(int, __cxa_thread_at_quick_exit_impl(void (*dtor)(void*), void* dso_symbol)) {
    TRACE("__cxa_thread_at_quick_exit_impl()");
    tls_atexit_quick(thrd_current(), dtor, NULL, dso_symbol);
    return 0;
}

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
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * CRT Functions 
 */

//#define __TRACE

#include <assert.h>
#include <ddk/utils.h>
#include <os/osdefs.h>
#include <math.h>
#include <stdlib.h>
#include <threads.h>
#include "../threads/tss.h"

#ifndef __INTERNAL_FUNC_DEFINED
#define __INTERNAL_FUNC_DEFINED
typedef void(*_PVFV)(void);
typedef int(*_PIFV)(void);
typedef void(*_PVFI)(int);
typedef void(*_PVTLS)(void*, unsigned long, void*);
#endif

extern void StdioCleanup(void);
extern void tss_atexit(_In_ thrd_t threadID, _In_ void (*atExitFn)(void*), _In_ void* argument, _In_ void* dsoHandle);
extern void tss_atexit_quick(_In_ thrd_t threadID, _In_ void (*atExitFn)(void*), _In_ void* argument, _In_ void* dsoHandle);

static int              g_cleanupPerformed = 0;
static const uintptr_t* g_moduleEntries    = NULL;

static void (*__cxa_primary_cleanup)(void);
static void (*__cxa_primary_tls_thread_init)(void);
static void (*__cxa_primary_tls_thread_finit)(void);

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
    _In_ void*          dsoHandle,
    _In_ unsigned long  reason))
{
    TRACE("__cxa_callinitializers_tls()");
    while (pfbegin < pfend) {
        if (*pfbegin != NULL) {
            TRACE(" > invoking 0x%" PRIxIN, *pfbegin);
            (**pfbegin)(dsoHandle, reason, NULL);
        }
        ++pfbegin;
    }
}

void CRTHIDE
__cxa_exithandlers(
    _In_ int exitCode,
    _In_ int quickCleanup,
    _In_ int executeAtExit,
    _In_ int cleanupRuntime)
{
    TRACE("__cxa_exithandlers()");
    // Avoid recursive calls or anything to this
    if (g_cleanupPerformed != 0) {
        return;
    }
    g_cleanupPerformed = 1;

    // Run dynamic crt for current thread + primary application
    if (!quickCleanup) {
        tss_cleanup(thrd_current(), NULL, exitCode);
        tss_cleanup(UUID_INVALID, NULL, exitCode);
    } else {
        tss_cleanup_quick(thrd_current(), NULL, exitCode);
        tss_cleanup_quick(UUID_INVALID, NULL, exitCode);
    }

    // Run dynamic/static for all modules
    if (!quickCleanup) {
        // Run at-exit lists for all the modules
        if (executeAtExit != 0) {
            for (int i = 0; g_moduleEntries != NULL && g_moduleEntries[i] != 0; i++) {
                ((void (*)(int))g_moduleEntries[i])(DLL_ACTION_FINALIZE);
            }
            // Cleanup primary app
            __cxa_primary_cleanup();
        }
    }

    // Cleanup crt if asked
    if (cleanupRuntime != 0) {
        StdioCleanup();
    }
    tls_destroy(tls_current());
}

/* __cxa_threadinitialize
 * Initializes thread storage runtime for all loaded modules */
CRTDECL(void, __cxa_threadinitialize(void))
{
    TRACE("__cxa_threadinitialize()");
    fpreset();

    for (int i = 0; g_moduleEntries != NULL && g_moduleEntries[i] != 0; i++) {
        ((void (*)(int))g_moduleEntries[i])(DLL_ACTION_THREADATTACH);
    }
    __cxa_primary_tls_thread_init();
}

/* __cxa_threadfinalize
 * Finalizes thread storage runtime for all loaded modules */
CRTDECL(void, __cxa_threadfinalize(void))
{
    TRACE("__cxa_threadfinalize()");
    for (int i = 0; g_moduleEntries != NULL && g_moduleEntries[i] != 0; i++) {
        ((void (*)(int))g_moduleEntries[i])(DLL_ACTION_THREADDETACH);
    }
    __cxa_primary_tls_thread_finit();
}

CRTDECL(void, __cxa_tls_thread_cleanup(void *Dso))
{
    TRACE("__cxa_tls_thread_cleanup()");
    tss_cleanup(thrd_current(), Dso, 0);
}

CRTDECL(void, __cxa_tls_module_cleanup(void *Dso))
{
    TRACE("__cxa_tls_module_cleanup()");
    tss_cleanup(UUID_INVALID, Dso, 0);
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
    tss_atexit(UUID_INVALID, Function, Argument, Dso);
    return 0;
}
CRTDECL(int, __cxa_at_quick_exit(void (*Function)(void*), void *Dso)) {
    TRACE("__cxa_at_quick_exit()");
    tss_atexit_quick(UUID_INVALID, Function, NULL, Dso);
    return 0;
}

/* __cxa_thread_atexit_impl/__cxa_thread_at_quick_exit_impl
 * C++ At-Exit implementation for thread specific cleanup. */
CRTDECL(int, __cxa_thread_atexit_impl(void (*dtor)(void*), void* arg, void* dso_symbol)) {
    TRACE("__cxa_thread_atexit_impl()");
    tss_atexit(thrd_current(), dtor, arg, dso_symbol);
    return 0;
}

CRTDECL(int, __cxa_thread_at_quick_exit_impl(void (*dtor)(void*), void* dso_symbol)) {
    TRACE("__cxa_thread_at_quick_exit_impl()");
    tss_atexit_quick(thrd_current(), dtor, NULL, dso_symbol);
    return 0;
}

/* __cxa_runinitializers 
 * C++ Initializes library C++ runtime for all loaded modules */
CRTDECL(void, __cxa_runinitializers(
    _In_ const uintptr_t* libraries,
    _In_ void (*module_init)(void), 
    _In_ void (*module_cleanup)(void),
    _In_ void (*module_thread_init)(void),
    _In_ void (*module_thread_finit)(void)))
{
    TRACE("__cxa_runinitializers(modules=0x%" PRIxIN ")", processInformation);
    fpreset();
    
    __cxa_primary_cleanup          = module_cleanup;
    __cxa_primary_tls_thread_init  = module_thread_init;
    __cxa_primary_tls_thread_finit = module_thread_finit;

    g_moduleEntries = libraries;
    for (int i = 0; g_moduleEntries != NULL && g_moduleEntries[i] != 0; i++) {
        TRACE("__cxa_runinitializers: module entry 0x%" PRIxIN, g_moduleEntries[i]);
        ((void (*)(int))g_moduleEntries[i])(DLL_ACTION_INITIALIZE);
    }

    // Run global and primary thread setup for process
    TRACE("[__cxa_runinitializers] init primary module");
    module_init();
}

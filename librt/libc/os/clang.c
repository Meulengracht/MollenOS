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
#include <internal/_utils.h>
#include <math.h>
#include <threads.h>

#ifndef __INTERNAL_FUNC_DEFINED
#define __INTERNAL_FUNC_DEFINED
typedef void(*_PVFV)(void);
typedef int(*_PIFV)(void);
typedef void(*_PVFI)(int);
typedef void(*_PVTLS)(void*, unsigned long, void*);
#endif

extern int  __at_exit_impl(_In_ thrd_t threadID, _In_ void (*atExitFn)(void*), _In_ void* argument, _In_ void* dsoHandle);
extern int  __at_quick_exit_impl(_In_ thrd_t threadID, _In_ void (*atExitFn)(void*), _In_ void* argument, _In_ void* dsoHandle);
extern void __cxa_at_exit_run(thrd_t threadID, void* dsoHandle, int exitCode);

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

void CRTHIDE __cxa_exithandlers(void)
{
    TRACE("__cxa_exithandlers()");

    if (g_cleanupPerformed) {
        return;
    }
    g_cleanupPerformed = 1;

    // Run at-exit lists for all the modules
    for (int i = 0; g_moduleEntries != NULL && g_moduleEntries[i] != 0; i++) {
        ((void (*)(int))g_moduleEntries[i])(DLL_ACTION_FINALIZE);
    }

    // Cleanup primary app
    __cxa_primary_cleanup();
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

CRTDECL(void, __cxa_tls_thread_cleanup(void* dsoHandle))
{
    TRACE("__cxa_tls_thread_cleanup()");
    __cxa_at_exit_run(__crt_thread_id(), dsoHandle, 0);
}

CRTDECL(void, __cxa_tls_module_cleanup(void* dsoHandle))
{
    TRACE("__cxa_tls_module_cleanup()");
    __cxa_at_exit_run(UUID_INVALID, dsoHandle, 0);
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
CRTDECL(int, __cxa_atexit(void (*fn)(void*), void* argument, void* dsoHandle)) {
    TRACE("__cxa_atexit()");
    return __at_exit_impl(UUID_INVALID, fn, argument, dsoHandle);;
}
CRTDECL(int, __cxa_at_quick_exit(void (*fn)(void*), void* dsoHandle)) {
    TRACE("__cxa_at_quick_exit()");
    return __at_quick_exit_impl(UUID_INVALID, fn, NULL, dsoHandle);
}

/* __cxa_thread_atexit_impl/__cxa_thread_at_quick_exit_impl
 * C++ At-Exit implementation for thread specific cleanup. */
CRTDECL(int, __cxa_thread_atexit_impl(void (*dtor)(void*), void* arg, void* dsoHandle)) {
    TRACE("__cxa_thread_atexit_impl()");
    return __at_exit_impl(__crt_thread_id(), dtor, arg, dsoHandle);
}

CRTDECL(int, __cxa_thread_at_quick_exit_impl(void (*dtor)(void*), void* dsoHandle)) {
    TRACE("__cxa_thread_at_quick_exit_impl()");
    return __at_quick_exit_impl(__crt_thread_id(), dtor, NULL, dsoHandle);
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

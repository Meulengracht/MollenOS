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
 * MollenOS C - CRT Functions 
 */
#define __TRACE

/* Includes
 * - Library */
#include <os/osdefs.h>
#include <os/spinlock.h>
#include <os/process.h>
#include <os/utils.h>

#include <math.h>
#include <threads.h>
#include <stdlib.h>
#include <assert.h>
#include "../threads/tls.h"

/* CRT Definitions
 * CRT Constants, definitions and types. */
#define EXITFUNCTION_FREE               0
#define EXITFUNCTION_CXA                1
#define EXITFUNCTION_ONEXIT             2
#ifndef __INTERNAL_FUNC_DEFINED
#define __INTERNAL_FUNC_DEFINED
typedef void(*_PVFV)(void);
typedef int(*_PIFV)(void);
typedef void(*_PVFI)(int);
typedef void(*_PVTLS)(void*, unsigned long, void*);
#endif

/* ProcessGetModuleEntryPoints
 * Retrieves a list of loaded modules for the process and
 * their entry points. */
OsStatus_t CRTHIDE
ProcessGetModuleEntryPoints(
    _Out_ Handle_t ModuleList[PROCESS_MAXMODULES]);

/* RTExitFunction
 * Implementation support for at-exit functions. 
 * Structure containing information about the function to invoke on exit. */
typedef struct _RTExitFunction {
    int                      Type;
    union {
        struct {
            void (*Function)(void *Argument, int Status);
            void            *Argument;
            void            *DsoHandle;
        } Cxa;
        struct {
            void (*Function)(int Status, void *Argument);
            void            *Argument;
        } OnExit;
    }                        Handler;
} RTExitFunction_t;

/* RTExitFunctionList
 * Implementation support for at-exit functions.
 * Structure containing a fixed number of possible at-exist functions. */
typedef struct _RTExitFunctionList {
    unsigned                     Index;
    RTExitFunction_t             Functions[32];
    struct _RTExitFunctionList  *Link;
} RTExitFunctionList_t;

/* Externs
 * Declare any external cleanup function. */
__EXTERN void StdioCleanup(void);

/* tls_atexit
 * Registers a thread-specific at-exit handler for a given symbol. */
__EXTERN void
tls_atexit(
    _In_ thrd_t thr,
    _In_ void (*function)(void*),
    _In_ void *argument,
    _In_ void *dso_symbol);

/* Globals
 * Statically defined globals for keeping track. */
static Handle_t ModuleList[PROCESS_MAXMODULES]      = { 0 };

static RTExitFunctionList_t  ExitFunctionQuickHead  = { 0 };
static RTExitFunctionList_t *ExitFunctionsQuick     = &ExitFunctionQuickHead;

static RTExitFunctionList_t  ExitFunctionHead       = { 0 };
static RTExitFunctionList_t *ExitFunctions          = &ExitFunctionHead;
static uint64_t ExitFunctionsCalled                 = 0;
static Spinlock_t ExitFunctionsLock                 = SPINLOCK_INIT;
static int ExitFunctionsDone                        = 0;
static void (*PrimaryApplicationFinalizers)(void);
static void (*PrimaryApplicationTlsAttach)(void);

/* __CrtCallInitializers
 * */
CRTDECL(void, __CrtCallInitializers(_PVFV *pfbegin, _PVFV *pfend))
{
	while (pfbegin < pfend) {
		if (*pfbegin != NULL)
			(**pfbegin)();
		++pfbegin;
	}
}

/* __CrtCallInitializersEx
 * */
CRTDECL(int, __CrtCallInitializersEx(
	_PIFV *pfbegin,
	_PIFV *pfend))
{
	int ret = 0;
	while (pfbegin < pfend  && ret == 0) {
		if (*pfbegin != NULL)
			ret = (**pfbegin)();
		++pfbegin;
	}
	return ret;
}

/* __CrtCallInitializersTls
 * */
CRTDECL(void, __CrtCallInitializersTls(
	_In_ _PVTLS*        pfbegin,
	_In_ _PVTLS*        pfend,
    _In_ void*          dso_handle,
    _In_ unsigned long  reason))
{
	while (pfbegin < pfend) {
		if (*pfbegin != NULL)
			(**pfbegin)(dso_handle, reason, NULL);
		++pfbegin;
	}
}

/* __CrtNewExitFunction
 * Finds a free function pointer that can be used for storing
 * a destructor function (global) */
RTExitFunction_t* CRTHIDE
__CrtNewExitFunction(
    _In_ RTExitFunctionList_t  **ListPointer)
{
    // Variables
    RTExitFunctionList_t *ListPrevious  = NULL;
    RTExitFunctionList_t *ListIterator  = NULL;
    RTExitFunction_t *ExitFunction      = NULL;
    unsigned i;

    // Sanitize the exit-functions for synchronous
    if (ExitFunctionsDone != 0) {
        return NULL;
    }

    // Iterate and find a free spot
    for (ListIterator = *ListPointer; ListIterator != NULL; 
         ListPrevious = ListIterator, ListIterator = ListIterator->Link) {
        for (i = ListIterator->Index; i > 0; --i) {
            if (ListIterator->Functions[i - 1].Type != EXITFUNCTION_FREE) {
                break;
            }
        }
        if (i > 0) {
            break;
        }
        ListIterator->Index = 0;
    }

    // If blocks were full, allocate a new block
    if (ListIterator == NULL || i == 32) {
        if (ListPrevious == NULL) {
            assert(ListIterator != NULL);
            ListPrevious = (RTExitFunctionList_t*)calloc(1, sizeof(RTExitFunctionList_t));
            if (ListPrevious != NULL) {
                ListPrevious->Link = *ListPointer;
                *ListPointer = ListPrevious;
            }
        }
        if (ListPrevious != NULL) {
            ExitFunction = &ListPrevious->Functions[0];
            ListPrevious->Index = 1;
        }
    }
    else {
        ExitFunction = &ListIterator->Functions[i];
        ListIterator->Index++;
    }

    // Mark entry taken
    if (ExitFunction != NULL) {
        ExitFunction->Type = EXITFUNCTION_CXA;
        ExitFunctionsCalled++;
    }
    return ExitFunction;
}

/* __CrtAtExit
 * */
int CRTHIDE
__CrtAtExit(
    _In_ void (*Function)(void*), 
    _In_ void                   *Argument,
    _In_ void                   *Dso,
    _In_ RTExitFunctionList_t  **List)
{
    // Variables
    RTExitFunction_t *ExitFunction = NULL;

    // Instantiate a new function structure, locked
    SpinlockAcquire(&ExitFunctionsLock);
    ExitFunction = __CrtNewExitFunction(List);
    if (ExitFunction == NULL) {
        SpinlockRelease(&ExitFunctionsLock);
        return -1;
    }

    // Update information
    ExitFunction->Type = EXITFUNCTION_CXA;
    ExitFunction->Handler.Cxa.Function = (void(*)(void*, int))Function;
    ExitFunction->Handler.Cxa.Argument = Argument;
    ExitFunction->Handler.Cxa.DsoHandle = Dso;
    SpinlockRelease(&ExitFunctionsLock);
    return 0;
}

/* __CrtCallExitHandlers
 * */
void CRTHIDE
__CrtCallExitHandlers(
    _In_ int Status,
    _In_ int Quick,
    _In_ int DoAtExit,
    _In_ int CleanupCrt)
{
    // Variables
    RTExitFunctionList_t **ListPointer = NULL;

    // Initialize list pointer
    ListPointer = ((Quick == 0) ? &ExitFunctions : &ExitFunctionsQuick);

    // Do this in a while loop to handle recursive
    // calls to Exit
    while (1) {
        RTExitFunctionList_t *Current = NULL;
        SpinlockAcquire(&ExitFunctionsLock);

    Cleanup:
        Current = *ListPointer;
        if (Current == NULL) {
            // Done, exit
            ExitFunctionsDone = 1;
            SpinlockRelease(&ExitFunctionsLock);
            break;
        }

        // Iterate all indices
        while (Current->Index > 0) {
            // Variables
            RTExitFunction_t *const Function = &Current->Functions[--Current->Index];
            uint64_t __ExitFunctionsCalled = ExitFunctionsCalled;

            // Unlock while calling the handler
            SpinlockRelease(&ExitFunctionsLock);
            switch (Function->Type) {
                case EXITFUNCTION_FREE: {
                    break;
                }
                case EXITFUNCTION_CXA: {
                    // Mark entry as free before invoking
                    Function->Type = EXITFUNCTION_FREE;
                    Function->Handler.Cxa.Function(Function->Handler.Cxa.Argument, Status);
                } break;
                case EXITFUNCTION_ONEXIT: {
                    Function->Handler.OnExit.Function(Status, Function->Handler.OnExit.Argument);
                } break;
            }

            // Reacquire lock
            SpinlockAcquire(&ExitFunctionsLock);
            if (__ExitFunctionsCalled != ExitFunctionsCalled) {
                goto Cleanup;
            }
        }

        // Move on to next block
        *ListPointer = Current->Link;
        if (*ListPointer != NULL) {
            free(Current);
        }
        SpinlockRelease(&ExitFunctionsLock);
    }

    // Run at-exit lists
    if (DoAtExit != 0) {
        for (int i = 0; i < PROCESS_MAXMODULES; i++) {
            if (ModuleList[i] == NULL) {
                break;
            }
            ((void (*)(int))ModuleList[i])(DLL_ACTION_FINALIZE);
        }
        // Cleanup primary app
        PrimaryApplicationFinalizers();
    }
    
    // Cleanup CRT if asked
    if (CleanupCrt != 0) {
        tls_cleanup(thrd_current());
        StdioCleanup();
        tls_destroy(tls_current());
    }
}

/* __cxa_atexit/__cxa_at_quick_exit 
 * C++ At-Exit implementation for registering of exit-handlers. */
CRTDECL(int, __cxa_atexit(void (*Function)(void*), void *Argument, void *Dso)) {
  return __CrtAtExit(Function, Argument, Dso, &ExitFunctions);
}
CRTDECL(int, __cxa_at_quick_exit(void (*Function)(void*), void *Dso)) {
  return __CrtAtExit(Function, NULL, Dso, &ExitFunctionsQuick);
}

/* __cxa_runinitializers 
 * C++ Initializes library C++ runtime for all loaded modules */
CRTDECL(void, __cxa_runinitializers(
    _In_ void (*Initializer)(void), 
    _In_ void (*Finalizer)(void), 
    _In_ void (*TlsAttachFunction)(void)))
{
    // Initialize math state
    fpreset();

    // Get modules available
    if (ProcessGetModuleEntryPoints(ModuleList) == OsSuccess) {
        for (int i = 0; i < PROCESS_MAXMODULES; i++) {
            if (ModuleList[i] == NULL) {
                break;
            }
            ((void (*)(int))ModuleList[i])(DLL_ACTION_INITIALIZE);
        }
    }

    // Run callers initializer
    Initializer();
    PrimaryApplicationFinalizers = Finalizer;
    PrimaryApplicationTlsAttach = TlsAttachFunction;
}

/* __cxa_threadinitialize
 * Initializes thread storage runtime for all loaded modules */
CRTDECL(void, __cxa_threadinitialize(void))
{
    // Initialize math
    fpreset();

    // Get modules available
    if (ProcessGetModuleEntryPoints(ModuleList) == OsSuccess) {
        for (int i = 0; i < PROCESS_MAXMODULES; i++) {
            if (ModuleList[i] == NULL) {
                break;
            }
            ((void (*)(int))ModuleList[i])(DLL_ACTION_THREADATTACH);
        }
    }

    // __CrtAttachTlsBlock for primary application
    PrimaryApplicationTlsAttach();
}

/* __cxa_finalize
 * C++ Cleanup implementation for process specific cleanup. */
CRTDECL(void, __cxa_finalize(void *Dso))
{
    // Variables
    RTExitFunctionList_t *List  = NULL;

    // Acquire lock
    SpinlockAcquire(&ExitFunctionsLock);
    
Cleanup:
    // Iterate all normal termination handlers and call them
    for (List = ExitFunctions; List != NULL; List = List->Link) {
        RTExitFunction_t *Function = NULL;
        if (List->Index == 0) {
            continue;
        }

        for (Function = &List->Functions[List->Index - 1];
             Function >= &List->Functions[0]; --Function) {
            if ((Dso == NULL || Dso == Function->Handler.Cxa.DsoHandle)
                && Function->Type == EXITFUNCTION_CXA) {
                uint64_t __ExitFunctionsCalled = ExitFunctionsCalled;

                // Mark free before calling
                Function->Type = EXITFUNCTION_FREE;
                SpinlockRelease(&ExitFunctionsLock);
                Function->Handler.Cxa.Function(Function->Handler.Cxa.Argument, 0);
                SpinlockAcquire(&ExitFunctionsLock);

                // Did new things get registered?
                if (__ExitFunctionsCalled != ExitFunctionsCalled) {
                    goto Cleanup;
                }
            }
        }
    }

    // Also iterate and remove quick termination handlers
    for (List = ExitFunctionsQuick; List != NULL; List = List->Link) {
        RTExitFunction_t *Function = NULL;
        if (List->Index == 0) {
            continue;
        }
        
        for (Function = &List->Functions[List->Index - 1];
             Function >= &List->Functions[0]; --Function) {
            if (Dso == NULL || Dso == Function->Handler.Cxa.DsoHandle) {
                Function->Type = EXITFUNCTION_FREE;
            }
        }
    }
}

/* __cxa_thread_atexit_impl
 * C++ At-Exit implementation for thread specific cleanup. */
CRTDECL(int, __cxa_thread_atexit_impl(void (*dtor)(void*), void* arg, void* dso_symbol)) {
    tls_atexit(thrd_current(), dtor, arg, dso_symbol);
    return 0;
}


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

/* Includes
 * - Library */
#include <os/osdefs.h>
#include <os/spinlock.h>
#include <threads.h>
#include <assert.h>

/* CRT Definitions
 * CRT Constants, definitions and types. */
#define EXITFUNCTION_FREE               0
#define EXITFUNCTION_CXA                1
#define EXITFUNCTION_ONEXIT             2
typedef void(*_PVFV)(void);
typedef int(*_PIFV)(void);
typedef void(*_PVFI)(int);

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

/* Globals
 * Statically defined globals for keeping track. */
static RTExitFunctionList_t  ExitFunctionQuickHead  = { 0 };
static RTExitFunctionList_t *ExitFunctionsQuick     = &ExitFunctionsQuick;

static RTExitFunctionList_t  ExitFunctionHead       = { 0 };
static RTExitFunctionList_t *ExitFunctions          = &ExitFunctionHead;
static uint64_t ExitFunctionsCalled                 = 0;
static Spinlock_t ExitFunctionsLock                 = SPINLOCK_INIT;
static int ExitFunctionsDone                        = 0;

/* __CrtCallInitializers
 * */
void __CrtCallInitializers(
	_PVFV *pfbegin,
	_PVFV *pfend)
{
	while (pfbegin < pfend) {
		if (*pfbegin != NULL)
			(**pfbegin)();
		++pfbegin;
	}
}

/* __CrtCallInitializersEx
 * */
int __CrtCallInitializersEx(
	_PIFV *pfbegin,
	_PIFV *pfend)
{
	int ret = 0;
	while (pfbegin < pfend  && ret == 0) {
		if (*pfbegin != NULL)
			ret = (**pfbegin)();
		++pfbegin;
	}
	return ret;
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

    // Cleanup CRT if asked
    if (CleanupCrt != 0) {
        StdioCleanup();
        tls_cleanup(thrd_current());
    }

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
        // @todo
    }
}

/* __cxa_atexit/__cxa_at_quick_exit 
 * C++ At-Exit implementation for registering of exit-handlers. */
int __cxa_atexit(void (*Function)(void*), void *Argument, void *Dso) {
  return __CrtAtExit(Function, Argument, Dso, &ExitFunctions);
}
int __cxa_at_quick_exit(void (*Function)(void*), void *Dso) {
  return __CrtAtExit(Function, NULL, Dso, &ExitFunctionsQuick);
}

/* __cxa_finalize
 * C++ Cleanup implementation for process specific cleanup. */
void __cxa_finalize(void *Dso)
{

}

/* __cxa_thread_atexit_impl
 * C++ At-Exit implementation for thread specific cleanup. */
int __cxa_thread_atexit_impl(void* dtor, void* obj, void* dso_symbol)
{
    return 0;
}


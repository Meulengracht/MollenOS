/*
 * MollenOS - Philip Meulengracht, Copyright 2011-2016
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
 * MollenOS CLib - Quick Exit Function
 */

#include <os/spinlock.h>
#include <stdlib.h>

#ifdef __clang__
extern void  __cxa_exithandlers(int exitCode, int quickCleanup, int executeAtExit, int cleanupRuntime);
extern int   __cxa_at_quick_exit(void (*Function)(void*), void *Dso);
extern void* __dso_handle;

int at_quick_exit(void(*fn)(void)) {
    return __cxa_at_quick_exit((void (*)(void*))fn, __dso_handle);
}

void quick_exit(int exitCode) {
    __cxa_exithandlers(exitCode, 1, 0, 0);
	_Exit(exitCode);
}
#else
/* We use a simple linked list structure of handles
 * quick-exit is a custom implementation of atexit and
 * thus we have to support much less */
typedef struct _QuickExitHandler {
	void (__CRTDECL *Handler)(void);
	struct _QuickExitHandler *Link;
} QuickExitHandler_t;

/* Spinlock protecting the list access
 * of quick-exit handlers */
static spinlock_t _GlbQuickExitLock = _SPN_INITIALIZER_NP(spinlock_plain);
static QuickExitHandler_t *_GlbQuickExitStack = NULL;

/* at_quick_exit 
 * Register a cleanup handler for
 * the quick_exit method */
int at_quick_exit(void(__CRTDECL *function)(void))
{
	/* Variables needed for registering
	 * a new handler */
	QuickExitHandler_t *Handler = NULL;

	/* Sanitize params first, we don't
	 * want null handlers */
	if (function == NULL) {
		_set_errno(EINVAL);
		return -1;
	}

	/* Acquire the lock, we don't
	 * want multiple threads accessing this */
	spinlock_acquire(&_GlbQuickExitLock);

	/* Allocate a new handler structure
	 * and init it */
	Handler = (QuickExitHandler_t*)malloc(sizeof(QuickExitHandler_t));
	Handler->Handler = function;
	Handler->Link = _GlbQuickExitStack;

	/* Update base link */
	_GlbQuickExitStack = Handler;

	/* Done, now we can release the lock */
	spinlock_release(&_GlbQuickExitLock);

	/* No errors! Wuhu */
	return 0;
}

/* quick_exit 
 * Calls all function in quick_exit and then
 * exits normally */
void quick_exit(int Status)
{
	/* Variables needed for iteration */
	QuickExitHandler_t *Handler = NULL;

	/* Iterate all handlers, call their
	 * cleanup functions */
	for (Handler = _GlbQuickExitStack; Handler != NULL; Handler = Handler->Link) {
		Handler->Handler();
	}

	/* Exit without calling any of the
	 * CRT related cleanup */
	_Exit(Status);
}
#endif

/* MollenOS
*
* Copyright 2011 - 2016, Philip Meulengracht
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
* MollenOS Visual C++ Implementation
*/

/* Includes */
#include "mvcxx.h"
#include "mscxx.h"

#include <stddef.h>
#include <string.h>
#include <assert.h>
#include <stdlib.h>

/* OS Includes */
#include <os/Thread.h>
#include <ds/list.h>

typedef EXCEPTION_DISPOSITION(*PVECTORED_EXCEPTION_HANDLER)(PEXCEPTION_POINTERS ExceptionPointers);

/* Globals */
Spinlock_t __VectorEhLock;
List_t *__VectorEhExceptions, *__VectorEhContinuations;

/* Private structure for entries */
typedef struct _RTL_VECTORED_HANDLER_ENTRY
{
	PVECTORED_EXCEPTION_HANDLER VectoredHandler;
	int Refs;
} RTL_VECTORED_HANDLER_ENTRY, *PRTL_VECTORED_HANDLER_ENTRY;

/* FUNCTIONS ***************************************************************/
_CRTXX_ABI void __CppInitVectoredEH(void)
{
	/* Initialize our two lists and the common lock */
	SpinlockReset(&__VectorEhLock);
	__VectorEhExceptions = ListCreate(KeyInteger, LIST_NORMAL);
	__VectorEhContinuations = ListCreate(KeyInteger, LIST_NORMAL);
}

/* Call a list of vectored EH handlers */
int CxxCallVectoredHandlers(PEXCEPTION_RECORD ExceptionRecord, 
	PCONTEXT Context, List_t *VectoredHandlerList)
{
	PRTL_VECTORED_HANDLER_ENTRY VectoredExceptionHandler = NULL;
	PVECTORED_EXCEPTION_HANDLER VectoredHandler = NULL;
	EXCEPTION_DISPOSITION HandlerReturn;
	EXCEPTION_POINTERS ExceptionInfo;
	ListNode_t *vNode;
	int HandlerRemoved;

	/*
	* Initialize these in case there are no entries,
	* or if no one handled the exception
	*/
	HandlerRemoved = 0;
	HandlerReturn = ExceptionContinueSearch;

	/* Set up the data to pass to the handler */
	ExceptionInfo.ExceptionRecord = ExceptionRecord;
	ExceptionInfo.ContextRecord = Context;

	/* Grab the lock */
	SpinlockAcquire(&__VectorEhLock);

	/* Iterate list */
	_foreach_nolink(vNode, VectoredHandlerList)
	{
		/* Get the struct */
		VectoredExceptionHandler = (PRTL_VECTORED_HANDLER_ENTRY)vNode->Data;

		/* Reference it so it doesn't go away while we are using it */
		VectoredExceptionHandler->Refs++;

		/* Drop the lock before calling the handler */
		SpinlockRelease(&__VectorEhLock);

		/*
		* Get the function pointer, decoding it so we will crash
		* if malicious code has altered it. That is, if something has
		* set VectoredHandler to a non-encoded pointer
		*/
		VectoredHandler = VectoredExceptionHandler->VectoredHandler;
		//VectoredHandler = RtlDecodePointer(VectoredExceptionHandler->VectoredHandler);

		/* Call the handler */
		HandlerReturn = VectoredHandler(&ExceptionInfo);

		/* Handler called -- grab the lock to dereference */
		SpinlockAcquire(&__VectorEhLock);

		/* Dereference and see if it got deleted */
		VectoredExceptionHandler->Refs--;
		if (VectoredExceptionHandler->Refs == 0)
		{
			/* It did -- do we have to free it now? */
			if (HandlerReturn == ExceptionContinueExecution)
			{
				/* We don't, just remove it from the list and break out */
				ListUnlinkNode(VectoredHandlerList, vNode);
				free(vNode);

				/* Done! */
				HandlerRemoved = 1;
				break;
			}

			/*
			* Get the next entry before freeing,
			* and remove the current one from the list
			*/
			ListNode_t *Next = ListUnlinkNode(VectoredHandlerList, vNode);
			free(vNode);
			vNode = Next;

			/* Free the entry outside of the lock, then reacquire it */
			SpinlockRelease(&__VectorEhLock);
			free(VectoredExceptionHandler);
			SpinlockAcquire(&__VectorEhLock);
		}
		else
		{
			/* No delete -- should we continue execution? */
			if (HandlerReturn == ExceptionContinueExecution) {
				/* Break out */
				break;
			}
			else {
				vNode = vNode->Link;
			}
		}
	}

	/* Let go of the lock now */
	SpinlockRelease(&__VectorEhLock);

	/* Anything to free? */
	if (HandlerRemoved) {
		/* Get rid of it */
		free(VectoredExceptionHandler);
	}

	/* Return whether to continue execution (ignored for continue handlers) */
	return (HandlerReturn == ExceptionContinueExecution) ? 1 : 0;
}

void *CxxAddVectoredHandler(long FirstHandler,
	PVECTORED_EXCEPTION_HANDLER VectoredHandler, List_t *VectoredHandlerList)
{
	/* Variables */
	PRTL_VECTORED_HANDLER_ENTRY VectoredHandlerEntry = NULL;
	DataKey_t Key;

	/* Unused */
	_CRT_UNUSED(FirstHandler);

	/* Allocate our structure */
	VectoredHandlerEntry = malloc(sizeof(RTL_VECTORED_HANDLER_ENTRY));
	
	/* Sanity */
	if (!VectoredHandlerEntry) 
		return NULL;

	/* Set it up, encoding the pointer for security */
	VectoredHandlerEntry->VectoredHandler = VectoredHandler;//RtlEncodePointer(VectoredHandler);
	VectoredHandlerEntry->Refs = 1;

	/* Lock the list before modifying it */
	SpinlockAcquire(&__VectorEhLock);

	/* Setup arbitrary key */
	Key.Value = 0;

	/*
	* While holding the list lock, insert the handler
	* at beginning or end of list according to caller.
	*/
	ListAppend(VectoredHandlerList, ListCreateNode(Key, Key, VectoredHandlerEntry));

	/* Done with the list, unlock it */
	SpinlockRelease(&__VectorEhLock);

	/* Return pointer to the structure as the handle */
	return VectoredHandlerEntry;
}

long CxxRemoveVectoredHandler(void *VectoredHandlerHandle, List_t *VectoredHandlerList)
{
	PRTL_VECTORED_HANDLER_ENTRY VectoredExceptionHandler = NULL;
	ListNode_t *vNode = NULL;
	int HandlerRemoved;
	int HandlerFound;

	/* Initialize these in case we don't find anything */
	HandlerRemoved = 0;
	HandlerFound = 0;

	/* Acquire list lock */
	SpinlockAcquire(&__VectorEhLock);

	/* Loop the list */
	_foreach(vNode, VectoredHandlerList)
	{
		/* Get the struct */
		VectoredExceptionHandler = (PRTL_VECTORED_HANDLER_ENTRY)vNode->Data;

		/* Does it match? */
		if (VectoredExceptionHandler == VectoredHandlerHandle)
		{
			/*
			* Great, this means it is a valid entry.
			* However, it may be in use by the exception
			* dispatcher, so we have a ref count to respect.
			* If we can't remove it now then it will be done
			* right after it is not in use anymore.
			*
			* Caller is supposed to keep track of if it has deleted the
			* entry and should not call us twice for the same entry.
			* We could maybe throw in some kind of ASSERT to detect this
			* if this was to become a problem.
			*/
			VectoredExceptionHandler->Refs--;
			if (VectoredExceptionHandler->Refs == 0)
			{
				/*
				* Get the next entry before freeing,
				* and remove the current one from the list
				*/
				ListUnlinkNode(VectoredHandlerList, vNode);
				free(vNode);

				/* Mark removed */
				HandlerRemoved = 1;
			}

			/* Found what we are looking for, stop searching */
			HandlerFound = 1;
			break;
		}
	}

	/* Done with the list, let go of the lock */
	SpinlockRelease(&__VectorEhLock);

	/* Can we free what we found? */
	if (HandlerRemoved) {
		/* Do it */
		free(VectoredExceptionHandler);
	}

	/* Return whether we found it */
	return (long)HandlerFound;
}

/* This is simply a wrapper call using the exception list
 * for calling argument */
int RtlCallVectoredExceptionHandlers(PEXCEPTION_RECORD ExceptionRecord, PCONTEXT Context)
{
	/* Call the shared routine */
	return CxxCallVectoredHandlers(ExceptionRecord, Context, __VectorEhExceptions);
}

/* This is simply a wrapper call using the continue list
 * for calling argument */
void RtlCallVectoredContinueHandlers(PEXCEPTION_RECORD ExceptionRecord, PCONTEXT Context)
{
	/*
	* Call the shared routine (ignoring result,
	* execution always continues at this point)
	*/
	CxxCallVectoredHandlers(ExceptionRecord, Context, __VectorEhContinuations);
}

void *RtlAddVectoredExceptionHandler(unsigned long FirstHandler, 
	PVECTORED_EXCEPTION_HANDLER VectoredHandler)
{
	/* Call the shared routine */
	return CxxAddVectoredHandler(FirstHandler,
		VectoredHandler, __VectorEhExceptions);
}

void *RtlAddVectoredContinueHandler(unsigned long FirstHandler,
	PVECTORED_EXCEPTION_HANDLER VectoredHandler)
{
	/* Call the shared routine */
	return CxxAddVectoredHandler(FirstHandler,
		VectoredHandler, __VectorEhContinuations);
}

//DECLSPEC_HOTPATCH
long RtlRemoveVectoredExceptionHandler(void *VectoredHandlerHandle)
{
	/* Call the shared routine */
	return CxxRemoveVectoredHandler(VectoredHandlerHandle, __VectorEhExceptions);
}

//DECLSPEC_HOTPATCH
long RtlRemoveVectoredContinueHandler(void *VectoredHandlerHandle)
{
	/* Call the shared routine */
	return CxxRemoveVectoredHandler(VectoredHandlerHandle, __VectorEhContinuations);
}

/* EOF */

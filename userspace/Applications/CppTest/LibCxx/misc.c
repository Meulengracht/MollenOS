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
#include <stdlib.h>
#include <stddef.h>
#include <string.h>

/* OS - Includes */
#include <os/Thread.h>

/* This function creates a new exception record based
* on the given information and raises the exception */
void CxxRaiseException(uint32_t dwExceptionCode, uint32_t dwExceptionFlags,
	uint32_t nNumberOfArguments, unsigned long *lpArguments)
{
	/* Variables */
	EXCEPTION_RECORD ExceptionRecord;

	/* Setup the exception record */
	ExceptionRecord.ExceptionCode = dwExceptionCode;
	ExceptionRecord.ExceptionRecord = NULL;
	ExceptionRecord.ExceptionAddress = (void*)CxxRaiseException;
	ExceptionRecord.ExceptionFlags = dwExceptionFlags & EXCEPTION_NONCONTINUABLE;

	/* Check if we have arguments */
	if (!lpArguments)
	{
		/* We don't */
		ExceptionRecord.NumberParameters = 0;
	}
	else
	{
		/* We do, normalize the count */
		if (nNumberOfArguments > EXCEPTION_MAXIMUM_PARAMETERS) {
			nNumberOfArguments = EXCEPTION_MAXIMUM_PARAMETERS;
		}

		/* Set the count of parameters and copy them */
		ExceptionRecord.NumberParameters = nNumberOfArguments;

		/* Do a simple copy */
		memcpy(ExceptionRecord.ExceptionInformation,
			lpArguments, nNumberOfArguments * sizeof(unsigned long));
	}

	/* Now actually raise the exception
	 * all is now converted to an ExceptionRecord */
}

/* This is the function that actually throws
 * the exception to the program */
void CxxThrowException(InternalException_t *ExcObject, const CxxExceptionType_t *ExcType)
{
	/* Variables needed for
	* constructing a new exception frame */
	unsigned long Arguments[3];

	Arguments[0] = CXX_FRAME_MAGIC_VC6;
	Arguments[1] = (unsigned long)ExcObject;
	Arguments[2] = (unsigned long)ExcType;

	/* Redirect */
	CxxRaiseException(CXX_EXCEPTION, EH_NONCONTINUABLE, 3, Arguments);
}

/* Install a handler to be called when terminate() is called.
 * The previously installed handler function, if any. */
CxxTerminateFunction __cdecl _set_terminate(CxxTerminateFunction func)
{
	ThreadLocalStorage_t *Data = TLSGetCurrent();
	CxxTerminateFunction previous = (CxxTerminateFunction)Data->TerminateHandler;
	Data->TerminateHandler = (void*)func;
	return previous;
}

/* Retrieves the current termination handler
 * for the current thread */
CxxTerminateFunction __cdecl _get_terminate(void)
{
	ThreadLocalStorage_t *Data = TLSGetCurrent();
	return (CxxTerminateFunction)Data->TerminateHandler;
}

/* Install a handler to be called when unexpected() is called.
 * The previously installed handler function, if any. */
CxxUnexpectedFunction __cdecl _set_unexpected(CxxUnexpectedFunction func)
{
	ThreadLocalStorage_t *Data = TLSGetCurrent();
	CxxUnexpectedFunction previous = (CxxUnexpectedFunction)Data->UnexpectedHandler;
	Data->UnexpectedHandler = (void*)func;
	return previous;
}

/* Retrieves the current unexpected handler
 * for the current thread */
CxxUnexpectedFunction __cdecl _get_unexpected(void)
{
	ThreadLocalStorage_t *Data = TLSGetCurrent();
	return (CxxUnexpectedFunction)Data->UnexpectedHandler;
}

/* Installs a SE Translator function and 
 * returns the current if there were any */
CxxSETranslatorFunction __cdecl _set_se_translator(CxxSETranslatorFunction func)
{
	ThreadLocalStorage_t *Data = TLSGetCurrent();
	CxxSETranslatorFunction previous = (CxxSETranslatorFunction)Data->SeTranslator;
	Data->SeTranslator = (void*)func;
	return previous;
}

/* This is the default handler 
 * for uncaught exceptions */
void __cdecl terminate(void)
{
	/* Access the local thread 
	 * storage and get the termination handler */
	ThreadLocalStorage_t *Data = TLSGetCurrent();

	/* Only if it exists ofc */
	if (Data->TerminateHandler) 
		((CxxTerminateFunction)Data->TerminateHandler)();

	/* Now abort! */
	abort();
}

/* This is the default handler 
 * for uncaught exceptions */
void __cdecl unexpected(void)
{
	/* Access the local thread 
	 * storage and get the termination handler */
	ThreadLocalStorage_t *Data = TLSGetCurrent();

	/* Only if it exists ofc */
	if (Data->UnexpectedHandler) 
		((CxxUnexpectedFunction)Data->UnexpectedHandler)();

	/* Terminate ! */
	terminate();
}
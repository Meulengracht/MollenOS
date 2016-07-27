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
#include "../../crt/msvc/abi/mvcxx.h"
#include <stddef.h>
#include <string.h>
#include <assert.h>

/* OS Includes */
#include <os/MollenOS.h>
#include <os/Thread.h>

#ifdef LIBC_KERNEL
void __ExceptionLibCEmpty(void)
{
}
#else


/* Does the final handling before actually throwing 
 * an exception, there is no return from this function */
void RtlRaiseException(PEXCEPTION_RECORD ExceptionRecord)
{
	/* Variables */
	CONTEXT Context;
	uint32_t Status = 0;

	/* Capture the context */
	RtlCaptureContext(&Context);

	/* Save the exception address */
	ExceptionRecord->ExceptionAddress = _ReturnAddress();

	/* Write the context flag */
	Context.ContextFlags = CONTEXT_FULL;

	/* Check if user mode debugger is active */
	if (TLSGetCurrent()->IsDebugging) {
		/* Raise an exception immediately */
		Status = ZwRaiseException(ExceptionRecord, &Context, 1);
	}
	else
	{
		/* Dispatch the exception and check if we should continue */
		if (!RtlDispatchException(ExceptionRecord, &Context))
		{
			/* Raise the exception */
			Status = ZwRaiseException(ExceptionRecord, &Context, 0);
		}
		else
		{
			/* Continue, go back to previous context */
			Status = ZwContinue(&Context, 0);
		}
	}

	/* If we returned, raise a status */
	RtlRaiseStatus(Status);
}

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable:4717) // Disable the recursion warning
#endif

/* Raises status on the current thread 
 * this function is recursive */
void RtlRaiseStatus(uint32_t Status)
{
	/* Variables */
	EXCEPTION_RECORD ExceptionRecord;
	CONTEXT Context;

	/* Capture the context */
	RtlCaptureContext(&Context);

	/* Create an exception record */
	ExceptionRecord.ExceptionAddress = _ReturnAddress();
	ExceptionRecord.ExceptionCode = Status;
	ExceptionRecord.ExceptionRecord = NULL;
	ExceptionRecord.NumberParameters = 0;
	ExceptionRecord.ExceptionFlags = EXCEPTION_NONCONTINUABLE;

	/* Write the context flag */
	Context.ContextFlags = CONTEXT_FULL;

	/* Check if user mode debugger is active */
	if (TLSGetCurrent()->IsDebugging) {
		/* Raise an exception immediately */
		ZwRaiseException(&ExceptionRecord, &Context, 1);
	}
	else {
		/* Dispatch the exception */
		RtlDispatchException(&ExceptionRecord, &Context);

		/* Raise exception if we got here */
		Status = ZwRaiseException(&ExceptionRecord, &Context, 0);
	}

	/* If we returned, raise a status */
	RtlRaiseStatus(Status);
}

#ifdef _MSC_VER
#pragma warning(pop)
#endif

/* LowLevel Exception Functions */
uint32_t ZwContinue(PCONTEXT Context, int TestAlert)
{
	return 0;
}

uint32_t ZwRaiseException(PEXCEPTION_RECORD ExceptionRecord, PCONTEXT Context, int FirstChance)
{
	return 0;
}

#endif
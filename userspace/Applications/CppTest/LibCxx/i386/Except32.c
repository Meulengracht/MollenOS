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
#include "../mvcxx.h"
#include "../mscxx.h"

#include <stddef.h>
#include <string.h>
#include <assert.h>
#include <intrin.h>

/* OS Includes */
#include <os/Thread.h>

/*
* The following two names are automatically created by the linker for any
* image that has the safe exception table present.
*/
extern void *__safe_se_handler_table[]; /* base of safe handler entry table */
extern char  __safe_se_handler_count;  /* absolute symbol whose address is
									   the count of table entries */
typedef struct {
	uint32_t       Size;
	uint32_t       TimeDateStamp;
	uint16_t        MajorVersion;
	uint16_t        MinorVersion;
	uint32_t       GlobalFlagsClear;
	uint32_t       GlobalFlagsSet;
	uint32_t       CriticalSectionDefaultTimeout;
	uint32_t       DeCommitFreeBlockThreshold;
	uint32_t       DeCommitTotalFreeThreshold;
	uint32_t       LockPrefixTable;            // VA
	uint32_t       MaximumAllocationSize;
	uint32_t       VirtualMemoryThreshold;
	uint32_t       ProcessHeapFlags;
	uint32_t       ProcessAffinityMask;
	uint16_t        CSDVersion;
	uint16_t        Reserved1;
	uint32_t       EditList;                   // VA
	uint32_t		*SecurityCookie;
	void			*SEHandlerTable;
	uint32_t       SEHandlerCount;
} IMAGE_LOAD_CONFIG_DIRECTORY32_2;

const IMAGE_LOAD_CONFIG_DIRECTORY32_2 _load_config_used = {
	sizeof(IMAGE_LOAD_CONFIG_DIRECTORY32_2),
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,//&__security_cookie,
	__safe_se_handler_table,
	(uint32_t)(uint32_t*)&__safe_se_handler_count
};

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
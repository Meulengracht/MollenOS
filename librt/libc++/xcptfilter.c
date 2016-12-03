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
#include <stdint.h>
#include <float.h>
#include <signal.h>

/* OS - Includes */
#include <os/Thread.h>

typedef void(*sighandler_t)(int);
typedef void(*float_handler)(int, int);
static sighandler_t sighandlers[NSIG] = { SIG_DFL };

/* The exception codes are actually NTSTATUS values */
static const struct
{
	int status;
	int signal;
} float_exception_map[] = {
	{ EXCEPTION_FLT_DENORMAL_OPERAND, _FPE_DENORMAL },
	{ EXCEPTION_FLT_DIVIDE_BY_ZERO, _FPE_ZERODIVIDE },
	{ EXCEPTION_FLT_INEXACT_RESULT, _FPE_INEXACT },
	{ EXCEPTION_FLT_INVALID_OPERATION, _FPE_INVALID },
	{ EXCEPTION_FLT_OVERFLOW, _FPE_OVERFLOW },
	{ EXCEPTION_FLT_STACK_CHECK, _FPE_STACKOVERFLOW },
	{ EXCEPTION_FLT_UNDERFLOW, _FPE_UNDERFLOW },
};

/*
* @implemented
*/
EXCEPTION_DISPOSITION __cdecl XcptFilter(
	int ExceptionCode, struct _EXCEPTION_POINTERS *ExceptionPtrs)
{
	/* Variables */
	EXCEPTION_DISPOSITION RetVal = ExceptionContinueSearch;
	sighandler_t Handler;

	/* Unuused */
	_CRT_UNUSED(ExceptionCode);

	/* Sanitize pointers (/records) */
	if (!ExceptionPtrs || !ExceptionPtrs->ExceptionRecord)
		return RetVal;

	/* What kind of exception are we dealing with */
	switch (ExceptionPtrs->ExceptionRecord->ExceptionCode)
	{
	case EXCEPTION_ACCESS_VIOLATION:
		if ((Handler = sighandlers[SIGSEGV]) != SIG_DFL)
		{
			if (Handler != SIG_IGN)
			{
				sighandlers[SIGSEGV] = SIG_DFL;
				Handler(SIGSEGV);
			}

			/* Yea, go on */
			RetVal = ExceptionContinueExecution;
		}
		break;
		/* According to msdn,
		* the FPE signal handler takes as a second argument the type of
		* floating point exception.
		*/
	case EXCEPTION_FLT_DENORMAL_OPERAND:
	case EXCEPTION_FLT_DIVIDE_BY_ZERO:
	case EXCEPTION_FLT_INEXACT_RESULT:
	case EXCEPTION_FLT_INVALID_OPERATION:
	case EXCEPTION_FLT_OVERFLOW:
	case EXCEPTION_FLT_STACK_CHECK:
	case EXCEPTION_FLT_UNDERFLOW:
		if ((Handler = sighandlers[SIGFPE]) != SIG_DFL)
		{
			if (Handler != SIG_IGN)
			{
				unsigned int i;
				int float_signal = _FPE_INVALID;

				sighandlers[SIGFPE] = SIG_DFL;
				for (i = 0; i < sizeof(float_exception_map) /
					sizeof(float_exception_map[0]); i++)
				{
					if (float_exception_map[i].status ==
						(int)ExceptionPtrs->ExceptionRecord->ExceptionCode)
					{
						float_signal = float_exception_map[i].signal;
						break;
					}
				}
				((float_handler)Handler)(SIGFPE, float_signal);
			}
			
			/* Yea, go on */
			RetVal = ExceptionContinueExecution;
		}
		break;
	case EXCEPTION_ILLEGAL_INSTRUCTION:
	case EXCEPTION_PRIV_INSTRUCTION:
		if ((Handler = sighandlers[SIGILL]) != SIG_DFL)
		{
			if (Handler != SIG_IGN)
			{
				sighandlers[SIGILL] = SIG_DFL;
				Handler(SIGILL);
			}
			
			/* Yea, go on */
			RetVal = ExceptionContinueExecution;
		}
		break;
	}

	/* Done! */
	return RetVal;
}


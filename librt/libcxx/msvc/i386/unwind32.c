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
#include <intrin.h>
#include <assert.h>

/* OS Includes */
#include <os/Thread.h>

/* Private Structures */
typedef struct _NESTED_FRAME {
	EXCEPTION_REGISTRATION_RECORD Frame;
	EXCEPTION_REGISTRATION_RECORD *Previous;
} NESTED_FRAME;

/* Call an exception handler, setting up an exception frame to catch exceptions
 * happening during the handler execution. */
EXCEPTION_DISPOSITION CxxUnwindCallHandler(EXCEPTION_RECORD *Record,
	EXCEPTION_REGISTRATION_RECORD *Frame, CONTEXT *Ctx, 
	EXCEPTION_REGISTRATION_RECORD **Dispatcher, PEXCEPTION_HANDLER Handler, 
	PEXCEPTION_HANDLER NestedHandler) 
{
	/* Variables */
	NESTED_FRAME NewFrame;
	EXCEPTION_DISPOSITION Rc;

	/* Setup nested frame information */
	NewFrame.Frame.ExceptionHandler = NestedHandler;
	NewFrame.Previous = Frame;

	/* Push the frame 
	 * Call the handler and pop frame off */
	CxxPushFrame(&NewFrame.Frame);
	Rc = Handler(Record, Frame, Ctx, Dispatcher);
	CxxPopFrame(&NewFrame.Frame);

	/* Done! */
	return Rc;
}


/* Handler for exceptions happening inside an unwind handler. */
EXCEPTION_DISPOSITION CxxUnwindException(EXCEPTION_RECORD *Record, 
	EXCEPTION_REGISTRATION_RECORD *Frame, CONTEXT *Ctx, 
	EXCEPTION_REGISTRATION_RECORD **Dispatcher) 
{
	/* Unused */
	_CRT_UNUSED(Ctx);

	/* Sanity */
	if (!(Record->ExceptionFlags & (EH_UNWINDING | EH_EXIT_UNWIND)))
		return ExceptionContinueSearch;

	/* We shouldn't get here so we store faulty frame in dispatcher */
	*Dispatcher = ((NESTED_FRAME *)Frame)->Previous;
	return ExceptionCollidedUnwind;
}

/* The actual base unwind function, this function can throw 
 * exceptions as well */
void CxxUnwind(PEXCEPTION_REGISTRATION_RECORD EndFrame, void *Eip,
	PEXCEPTION_RECORD pRecord, uint32_t ReturnValue, CONTEXT *Ctx) 
{
	/* Variables */
	PEXCEPTION_REGISTRATION_RECORD Frame, Dispatch;
	EXCEPTION_RECORD Record, NewRecord;
	EXCEPTION_DISPOSITION Rc;
	uint32_t StackLow, StackHigh;

	/* Get local thread data */
	ThreadLocalStorage_t *tData = TLSGetCurrent();

	/* Unused */
	_CRT_UNUSED(ReturnValue);
	_CRT_UNUSED(Eip);

	/* Get the current stack limits */
	StackLow = (uint32_t)tData->StackLow;
	StackHigh = (uint32_t)tData->StackHigh;

	/* Check if we have already been given a exception
	 * record, otherwise we have to create one */
	if (!pRecord) {
		Record.ExceptionCode = EXCEPTION_STATUS_UNWIND;
		Record.ExceptionFlags = 0;
		Record.ExceptionRecord = NULL;
		Record.ExceptionAddress = (void*)Ctx->Eip;
		Record.NumberParameters = 0;
		pRecord = &Record;
	}

	/* Is it the last frame? */
	pRecord->ExceptionFlags |= EH_UNWINDING | (EndFrame ? 0 : EH_EXIT_UNWIND);

	/* Retrieve the exception frame list */
	Frame = (PEXCEPTION_REGISTRATION_RECORD)tData->ExceptionList;

	/* Iterate all exception frames untill 
	 * we reach the end of our target frame or end of list */
	while (Frame != EXCEPTION_CHAIN_END) 
	{
		/* Registration chain entries are never NULL */
		assert(Frame != NULL);

		/* If we have reached the target */
		if (Frame == EndFrame)
			ZwContinue(Ctx, 0);

		/* Validate frame address */
		if (EndFrame && (Frame > EndFrame)) {

			/* Setup invalid unwind exception */
			NewRecord.ExceptionCode = EXCEPTION_INVALID_UNWIND;
			NewRecord.ExceptionFlags = EH_NONCONTINUABLE;
			NewRecord.ExceptionRecord = pRecord;
			NewRecord.NumberParameters = 0;

			/* Fire! */
			RtlRaiseException(&NewRecord);  // Never returns
		}

		/* Make sure the registration frame is located within the stack */
		if ((uint32_t)Frame < StackLow
			|| (uint32_t)(Frame + 1) > StackHigh
			|| (int)Frame & 3) 
		{
			/* Setup invalid stack exception */
			NewRecord.ExceptionCode = EXCEPTION_INVALID_STACK;
			NewRecord.ExceptionFlags = EH_NONCONTINUABLE;
			NewRecord.ExceptionRecord = pRecord;
			NewRecord.NumberParameters = 0;

			/* Fire! */
			RtlRaiseException(&NewRecord);  // Never returns
		}
		else
		{
			/* Call Exception Handler */
			Rc = CxxUnwindCallHandler(pRecord, Frame,
				Ctx, &Dispatch, Frame->ExceptionHandler, CxxUnwindException);

			switch (Rc) {
			case ExceptionContinueSearch:
				break;

			case ExceptionCollidedUnwind:
				Frame = Dispatch;
				break;

			default:
				/* Invalid disposition returned! */
				NewRecord.ExceptionCode = EXCEPTION_INVALID_DISPOSITION;
				NewRecord.ExceptionFlags = EH_NONCONTINUABLE;
				NewRecord.ExceptionRecord = pRecord;
				NewRecord.NumberParameters = 0;

				/* Fire! */
				RtlRaiseException(&NewRecord);  // Never returns
			}
		}
		
		/* Skip to next frame */
		Frame = CxxPopFrame(Frame);
	}

	/* Check if we reached the end */
	if (EndFrame == EXCEPTION_CHAIN_END) {
		/* Unwind completed, so we don't exit */
		ZwContinue(Ctx, 0);
	}
	else {
		/* This is an exit_unwind or the frame wasn't present in the list */
		ZwRaiseException(pRecord, Ctx, 0);
	}
}

/* This function actually dispatches the exception to
* usermode handlers */
int RtlDispatchException(PEXCEPTION_RECORD ExceptionRecord, PCONTEXT Context)
{
	/* Variables */
	PEXCEPTION_REGISTRATION_RECORD RegistrationFrame, NestedFrame = NULL, Dispatch;
	EXCEPTION_RECORD NewRecord;
	EXCEPTION_DISPOSITION Disposition;
	uint32_t StackLow, StackHigh;

	/* Get local thread data */
	ThreadLocalStorage_t *tData = TLSGetCurrent();

	/* Perform vectored exception handling for user mode */
	if (RtlCallVectoredExceptionHandlers(ExceptionRecord, Context)) {
		/* Exception handled, now call vectored continue handlers */
		RtlCallVectoredContinueHandlers(ExceptionRecord, Context);

		/* Continue execution */
		return 1;
	}

	/* Get the current stack limits */
	StackLow = (uint32_t)tData->StackLow;
	StackHigh = (uint32_t)tData->StackHigh;

	/* Retrieve the exception frame list */
	RegistrationFrame = (PEXCEPTION_REGISTRATION_RECORD)tData->ExceptionList;

	/* Iterate all exception frames untill
	* we reach the end of our target frame or end of list */
	while (RegistrationFrame != EXCEPTION_CHAIN_END)
	{
		/* Registration chain entries are never NULL */
		assert(RegistrationFrame != NULL);

		/* Make sure the registration frame is located within the stack */
		if ((uint32_t)RegistrationFrame < StackLow
			|| (uint32_t)(RegistrationFrame + 1) > StackHigh
			|| (int)RegistrationFrame & 3)
		{
			/* Set invalid stack and return false */
			ExceptionRecord->ExceptionFlags |= EXCEPTION_STACK_INVALID;
			return 0;
		}

		/* Call Exception Handler */
		Disposition = CxxUnwindCallHandler(ExceptionRecord, RegistrationFrame,
			Context, &Dispatch, RegistrationFrame->ExceptionHandler, CxxUnwindException);

		/* Check if this is a nested frame */
		if (RegistrationFrame == NestedFrame)
		{
			/* Mask out the flag and the nested frame */
			ExceptionRecord->ExceptionFlags &= ~EXCEPTION_NESTED_CALL;
			NestedFrame = NULL;
		}

		/* Handle the dispositions */
		switch (Disposition)
		{
			/* Continue execution */
		case ExceptionContinueExecution:

			/* Check if it was non-continuable */
			if (ExceptionRecord->ExceptionFlags & EXCEPTION_NONCONTINUABLE)
			{
				/* Set up the exception record */
				NewRecord.ExceptionRecord = ExceptionRecord;
				NewRecord.ExceptionCode = EXCEPTION_NONCONTINUABLE_EXCEPTION;
				NewRecord.ExceptionFlags = EXCEPTION_NONCONTINUABLE;
				NewRecord.NumberParameters = 0;

				/* Raise the exception */
				RtlRaiseException(&NewRecord);
			}
			else {
				/* In user mode, call any registered vectored continue handlers */
				RtlCallVectoredContinueHandlers(ExceptionRecord, Context);

				/* Execution continues */
				return 1;
			}

			/* Continue searching */
		case ExceptionContinueSearch:
			break;

			/* Nested exception */
		case ExceptionNestedException:

			/* Turn the nested flag on */
			ExceptionRecord->ExceptionFlags |= EXCEPTION_NESTED_CALL;

			/* Update the current nested frame */
			if (Dispatch > NestedFrame) {
				/* Get the frame from the dispatcher context */
				NestedFrame = Dispatch;
			}
			break;

			/* Anything else */
		default:

			/* Set up the exception record */
			NewRecord.ExceptionRecord = ExceptionRecord;
			NewRecord.ExceptionCode = EXCEPTION_INVALID_DISPOSITION;
			NewRecord.ExceptionFlags = EXCEPTION_NONCONTINUABLE;
			NewRecord.NumberParameters = 0;

			/* Raise the exception */
			RtlRaiseException(&NewRecord);
			break;
		}

		/* Skip to next frame */
		RegistrationFrame = CxxPopFrame(RegistrationFrame);
	}

	/* Unhandled, return false */
	return 0;
}

/* Main Unwind function */
void RtlUnwind(PEXCEPTION_REGISTRATION_RECORD EndFrame, void *Eip,
	PEXCEPTION_RECORD Record, uint32_t ReturnValue) 
{
	/* Variables 
	 * Use a null context here */
	CONTEXT Ctx;

	/* Read Context */
	Ctx.ContextFlags = CONTEXT_FULL;
	RtlpCaptureContext(&Ctx);

	/* Pop the current arguments off */
	Ctx.Esp += sizeof(EndFrame) +
		sizeof(Eip) +
		sizeof(Record) +
		sizeof(ReturnValue);

	/* Update return address */
	Ctx.Eip = (uint32_t)_ReturnAddress();

	/* Set the new value for EAX */
	Ctx.Eax = ReturnValue;

	/* Call base unwind */
	CxxUnwind(EndFrame, Eip, Record, ReturnValue, &Ctx);
}
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

/* External to assembly function */
extern uint32_t __cdecl CallCxxFrameHandler(PEXCEPTION_RECORD Record, 
	EXCEPTION_REGISTRATION_RECORD *Frame,
	PCONTEXT Context, EXCEPTION_REGISTRATION_RECORD **Dispatch,
	const CxxFunctionDescriptor_t *Descriptor);


/* This converts some very VC++ version
 * specific stuff to a shared frame handler */
uint32_t __stdcall CxxHandleV8Frame(PEXCEPTION_RECORD Record, 
	EXCEPTION_REGISTRATION_RECORD *Frame, PCONTEXT Context, 
	EXCEPTION_REGISTRATION_RECORD **Dispatch, const CxxFunctionDescriptor_t *Descriptor)
{
	/* Stub Descriptor */
	CxxFunctionDescriptor_t StubDesc;

	/* If it's not a VC8 exception we 
	 * simply redirect it, then we don't 
	 * need to handle VC8 flags */
	if (Descriptor->Magic != CXX_FRAME_MAGIC_VC8)
		return CallCxxFrameHandler(Record, Frame, Context, Dispatch, Descriptor);

	/* Handle the VC8 flags, thats the difference
	 * between VC7 and VC8 */
	if ((Descriptor->Flags & FUNC_DESCR_SYNCHRONOUS) 
		&& (Record->ExceptionCode != CXX_EXCEPTION)) {
		/* Handle only c++ exceptions */
		return ExceptionContinueSearch;
	}

	/* Handle this as a VC7 exception */
	StubDesc = *Descriptor;
	StubDesc.Magic = CXX_FRAME_MAGIC_VC7;

	/* Redirect to shared */
	return CallCxxFrameHandler(Record, Frame, Context, Dispatch, &StubDesc);
}

/* Call a function with a given ebp */
#ifdef _MSC_VER
#pragma warning(disable:4731) // don't warn about modification of ebp
#endif
void *CxxCallEbp(void *Function, void *_Ebp)
{
	/* Variables */
	void *Result;

	/* Do some delicous inline 
	 * magic to call the function */
	__asm
	{
		mov eax, Function
		push ebx
		push ebp
		mov ebp, _Ebp
		call eax
		pop ebp
		pop ebx
		mov Result, eax
	}

	/* Done! */
	return Result;
}
#ifdef _MSC_VER
#pragma warning(default:4731)
#endif

/* Unwind the local function up to a given trylevel 
 * Pass -1 to LastLevel to unwind all */
void __cdecl CxxUnwindLocal(CxxExceptionFrame_t *Frame, 
	const CxxFunctionDescriptor_t *Descriptor, int LastLevel)
{
	/* Variables */
	void(*Handler)(void);
	int TryLevel = Frame->TryLevel;

	/* Iterate untill */
	while (TryLevel != LastLevel)
	{
		/* If our trylevel is negative OR above the 
		 * actual limit of the unwind, aboooort!! */
		if (TryLevel < 0 || (uint32_t)LastLevel >= Descriptor->UnwindCount) {
			//ERR("invalid trylevel %d\n", trylevel);
			terminate();
		}

		/* Store the handler */
		Handler = Descriptor->UnwindTable[TryLevel].Handler;

		/* Do we have a handler or is there none? */
		if (Handler)
		{
			//TRACE("calling unwind handler %p trylevel %d last %d ebp %p\n",
			//	Handler, TryLevel, LastLevel, &Frame->Ebp);
			CxxCallEbp(Handler, &Frame->Ebp);
		}

		/* Go to next (well.. previous) try level */
		TryLevel = Descriptor->UnwindTable[TryLevel].PreviousTryLevel;
	}

	/* Update try level */
	Frame->TryLevel = LastLevel;
}

/* This is the main handler of exception frames 
 * and calls the catch blocks etc etc */
uint32_t __cdecl CxxHandleFrame(PEXCEPTION_RECORD Record, CxxExceptionFrame_t *Frame,
	PCONTEXT Context, EXCEPTION_REGISTRATION_RECORD **Dispatch,
	const CxxFunctionDescriptor_t *Descriptor, EXCEPTION_REGISTRATION_RECORD* NestedFrame,
	int NestedTryLevel)
{
	/* Variables */
	CxxExceptionType_t *ExceptType;

	/* Sanity the magic header 
	 * it would be very weird if we got 
	 * something we shouldn't ... */
	if (Descriptor->Magic < CXX_FRAME_MAGIC_VC6 || Descriptor->Magic > CXX_FRAME_MAGIC_VC8)
	{
		//ERR("invalid frame magic %x\n", Descriptor->Magic);
		return ExceptionContinueSearch;
	}

	/* Sanity the type 
	 * Handle only c++ exceptions */
	if (Descriptor->Magic >= CXX_FRAME_MAGIC_VC8 &&
		(Descriptor->Flags & FUNC_DESCR_SYNCHRONOUS) && (Record->ExceptionCode != CXX_EXCEPTION))
		return ExceptionContinueSearch;

	/* Sanitize the record flags 
	 * Because we might have the EXIT unwind */
	if (Record->ExceptionFlags & (EH_UNWINDING | EH_EXIT_UNWIND))
	{
		/* If we are the root we should 
		 * do a local unwind */
		if (Descriptor->UnwindCount && !NestedTryLevel) 
			CxxUnwindLocal(Frame, Descriptor, -1);

		/* Done */
		return ExceptionContinueSearch;
	}

	/* Sanitize the tryblock count.. 
	 * if there are none then what are we doing */
	if (!Descriptor->TryBlockCount) 
		return ExceptionContinueSearch;

	/* Now, we only want to handle this exception
	 * if it's an ACTUAL c++ exception */
	if (Record->ExceptionCode == CXX_EXCEPTION)
	{
		/* Get the exception type information 
		 * It's located at index 2 */
		ExceptType = (CxxExceptionType_t*)Record->ExceptionInformation[2];

		/* Is there installed a custom CXX handler
		 * for this exception? */
		if (Record->ExceptionInformation[0] > CXX_FRAME_MAGIC_VC8 
			&& ExceptType->CustomerHandler)
		{
			/* Deep Call */
			return ExceptType->CustomerHandler(Record, Frame, Context, Dispatch,
				Descriptor, NestedTryLevel, NestedFrame, 0);
		}
	}
	else
	{
		ExceptType = NULL;
		//TRACE("handling C exception code %x  rec %p frame %p trylevel %d descr %p nested_frame %p\n",
		//	rec->ExceptionCode, rec, frame, frame->trylevel, Descriptor, nested_frame);
	}

	/* Handle the catch block */
	//call_catch_block(Record, Frame, Descriptor, Frame->TryLevel, ExceptType);

	/* Done! */
	return ExceptionContinueSearch;
}
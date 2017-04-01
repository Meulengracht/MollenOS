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

/* OS Includes */
#include <os/Thread.h>

/* Compute the this pointer for a base class of a given type */
void *CxxGetThisPointer(const CxxThisPtrOffsets_t *Offsets, void *Object);
extern void terminate(void);

/* Private Structures */

/* Exception frame for nested exceptions in catch block */
struct CxxCatchFunctionNestedFrame
{
	/* Standard exception frame */
	EXCEPTION_REGISTRATION_RECORD Frame;

	/* Previous record to restore in thread data */
	EXCEPTION_RECORD             *PreviousRecord;

	/* Frame of parent exception */
	CxxExceptionFrame_t          *CxxFrame;

	/* Descriptor of parent exception */
	const CxxFunctionDescriptor_t     *Descriptor;

	/* Current try level */
	int                           TryLevel;

	/* Record associated with frame */
	EXCEPTION_RECORD             *Record;
};

/* External to assembly function */
extern EXCEPTION_DISPOSITION __cdecl CallCxxFrameHandler(PEXCEPTION_RECORD Record,
	EXCEPTION_REGISTRATION_RECORD *Frame,
	PCONTEXT Context, EXCEPTION_REGISTRATION_RECORD **Dispatch,
	const CxxFunctionDescriptor_t *Descriptor);

/* Prototype */
EXCEPTION_DISPOSITION __cdecl CxxHandleFrame(PEXCEPTION_RECORD Record, CxxExceptionFrame_t *Frame,
	PCONTEXT Context, EXCEPTION_REGISTRATION_RECORD **Dispatch,
	const CxxFunctionDescriptor_t *Descriptor, EXCEPTION_REGISTRATION_RECORD* NestedFrame,
	int NestedTryLevel);

/* This converts some very VC++ version
 * specific stuff to a shared frame handler */
EXCEPTION_DISPOSITION __stdcall CxxHandleV8Frame(PEXCEPTION_RECORD Record,
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

/* Call a copy constructor */
void CxxCallCopyCtor(void *Function, void *This, void *Source, int HasVBase)
{
#ifdef _MSC_VER
	if (HasVBase)
	{
		__asm
		{
			mov ecx, This
			push 1
			push Source
			call Function
		}
	}
	else
	{
		__asm
		{
			mov ecx, This
			push Source
			call Function
		}
	}
#else
	if (has_vbase)
		/* in that case copy ctor takes an extra bool indicating whether to copy the base class */
		__asm__ __volatile__("pushl $1; pushl %2; call *%0"
		: : "r" (func), "c" (this), "r" (src) : "eax", "edx", "memory");
	else
		__asm__ __volatile__("pushl %2; call *%0"
		: : "r" (func), "c" (this), "r" (src) : "eax", "edx", "memory");
#endif
}

/* Call the destructor of the exception object */
void CxxCallDestructor(void *Function, void *Object)
{
#ifdef _MSC_VER
	__asm
	{
		mov ecx, Object
		call Function
	}
#else
	__asm__ __volatile__("call *%0" : : "r" (func), "c" (object) : "eax", "edx", "memory");
#endif
}

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable:4731)
/* continue execution to the specified address after exception is caught */
__forceinline void DECLSPEC_NORETURN CxxContinueCatch(CxxExceptionFrame_t *Frame, void *Addr)
{
	__asm
	{
		mov eax, Addr
		mov edx, Frame
		mov esp, [edx - 4]
		lea ebp, [edx + 12]
		jmp eax
	}

	/* Safety Catch */
	for (;;);
}
#pragma warning(pop)
#else
/* continue execution to the specified address after exception is caught */
static inline void DECLSPEC_NORETURN continue_after_catch(cxx_exception_frame* frame, void *addr)
{
	__asm__ __volatile__("movl -4(%0),%%esp; leal 12(%0),%%ebp; jmp *%1"
		: : "r" (frame), "a" (addr));
	for (;;); /* unreached */
}
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
			CxxCallEbp((void*)Handler, (void*)&Frame->Ebp);
		}

		/* Go to next (well.. previous) try level */
		TryLevel = Descriptor->UnwindTable[TryLevel].PreviousTryLevel;
	}

	/* Update try level */
	Frame->TryLevel = LastLevel;
}

/* Check if the exception type is caught by a given catch block, 
 * and return the type that matched */
const CxxTypeInfo_t *CxxFindCaughtType(CxxExceptionType_t *ExceptionType,
	const CxxCatchBlockInformation_t *CatchBlock)
{
	/* Variables */
	uint32_t i;

	/* Iterate the different types this exception
	 * is capable of mutating to */
	for (i = 0; i < ExceptionType->TypeTable->EntryCount; i++)
	{
		/* Retrieve the type information for this index */
		const CxxTypeInfo_t *Type = ExceptionType->TypeTable->Table[i];

		/* First of all, is this a match all type? */
		if (!CatchBlock->TypeInfo) 
			return Type;

		/* Is it the same type? */
		if (CatchBlock->TypeInfo != Type->TypeInfo)
		{
			/* Ok ok, we can still check the mangled name if it matches */
			if (strcmp(CatchBlock->TypeInfo->Mangled, Type->TypeInfo->Mangled))
				continue; /* Damn, no match! */
		}

		/* Type is the same, now check the flags */
		if ((ExceptionType->Flags & TYPE_FLAG_CONST) &&
			!(CatchBlock->Flags & TYPE_FLAG_CONST)) 
			continue;
		if ((ExceptionType->Flags & TYPE_FLAG_VOLATILE) &&
			!(CatchBlock->Flags & TYPE_FLAG_VOLATILE)) 
			continue;

		/* Wuhu!! Matched! */
		return Type;
	}

	/* Nope, didn't match */
	return NULL;
}

/* Copy the exception object where the catch block wants it */
void __cdecl CxxCopyException(void *Object, CxxExceptionFrame_t *Frame,
	const CxxCatchBlockInformation_t *CatchBlock, const CxxTypeInfo_t *Type)
{
	/* Variables */
	void **Destination;

	/* If there isn't any type information 
	 * or any exception information then skip */
	if (!CatchBlock->TypeInfo || !CatchBlock->TypeInfo->Mangled[0]) 
		return;

	/* Offset is 0?? */
	if (!CatchBlock->Offset) 
		return;

	/* Calculate destination */
	Destination = (void **)((char *)&Frame->Ebp + CatchBlock->Offset);

	/* If it's a pointer, just reference the pointer */
	if (CatchBlock->Flags & TYPE_FLAG_REFERENCE) {
		*Destination = CxxGetThisPointer(&Type->Offsets, Object);
	}
	else if (Type->Flags & CLASS_IS_SIMPLE_TYPE)
	{
		/* Copy the exception object */
		memmove(Destination, Object, Type->ObjectSize);

		/* If it is a pointer, adjust it */
		if (Type->ObjectSize == sizeof(void *))
			*Destination = CxxGetThisPointer(&Type->Offsets, *Destination);
	}
	else {
		/* Copy the object */
		if (Type->Copy)
			CxxCallCopyCtor(Type->Copy, Destination, 
				CxxGetThisPointer(&Type->Offsets, Object), 
				(Type->Flags & CLASS_HAS_VIRTUAL_BASE_CLASS));
		else
			memmove(Destination, CxxGetThisPointer(&Type->Offsets, Object), Type->ObjectSize);
	}
}

/* Handler for exceptions happening while calling a catch function */
EXCEPTION_DISPOSITION CxxCatchFunctionNestedHandler(EXCEPTION_RECORD *Record, 
	EXCEPTION_REGISTRATION_RECORD *Frame, CONTEXT *Context, 
	EXCEPTION_REGISTRATION_RECORD **Dispatcher)
{
	/* Cast the given exception registration record 
	 * to a nested frame record */
	struct CxxCatchFunctionNestedFrame *NestedFrame = 
		(struct CxxCatchFunctionNestedFrame *)Frame;

	/* Unused */
	_CRT_UNUSED(Dispatcher);

	if (Record->ExceptionFlags & (EH_UNWINDING | EH_EXIT_UNWIND)) {
		ThreadLocalStorage_t *tData = TLSGetCurrent();
		tData->ExceptionRecord = NestedFrame->PreviousRecord;
		return ExceptionContinueSearch;
	}

	//TRACE("got nested exception in catch function\n");

	/* We only handle C++ Exceptions */
	if (Record->ExceptionCode == CXX_EXCEPTION)
	{
		/* Get the record from the nested frame */
		PEXCEPTION_RECORD PreviousRecord = NestedFrame->Record;

		/* Rethrown exception or 'original'? */
		if (Record->ExceptionInformation[1] == 0 
			&& Record->ExceptionInformation[2] == 0)
		{
			/* Exception was rethrown */
			Record->ExceptionInformation[1] = PreviousRecord->ExceptionInformation[1];
			Record->ExceptionInformation[2] = PreviousRecord->ExceptionInformation[2];
			/*TRACE("detect rethrow: re-propagate: obj: %lx, type: %lx\n",
				Record->ExceptionInformation[1], Record->ExceptionInformation[2]); */
		}
		else {
			/* New exception in exception handler, destroy old */
			void *Object = (void*)PreviousRecord->ExceptionInformation[1];
			CxxExceptionType_t *Info = (CxxExceptionType_t*)PreviousRecord->ExceptionInformation[2];
			
			/* Debug
			TRACE("detect threw new exception in catch block - destroy old(obj: %p type: %p)\n",
				Object, Info); */

			/* Cleanup? */
			if (Info && Info->Cleanup)
				CxxCallDestructor(Info->Cleanup, Object);
		}
	}

	/* Handle the frame */
	return CxxHandleFrame(Record, NestedFrame->CxxFrame, Context,
		NULL, NestedFrame->Descriptor, &NestedFrame->Frame, NestedFrame->TryLevel);
}

/* Find and call the appropriate catch block for an exception
 * returns the address to continue execution to after the catch block was called */
void __cdecl CxxCallCatch(PEXCEPTION_RECORD Record, CxxExceptionFrame_t *Frame,
	const CxxFunctionDescriptor_t *Descriptor, int NestedTryLevel, CxxExceptionType_t *Info)
{
	/* Variables */
	struct CxxCatchFunctionNestedFrame NestedFrame;
	ThreadLocalStorage_t *tData = TLSGetCurrent();
	void *Addr, *Object = (void*)Record->ExceptionInformation[1];
	int TryLevel = Frame->TryLevel;
	uint32_t SaveEsp = ((uint32_t*)Frame)[-1];
	uint32_t i;
	int j;

	/* Iterate try blocks */
	for (i = 0; i < Descriptor->TryBlockCount; i++)
	{
		/* Retrieve the tryblock for this index */
		const CxxTryBlockInformation_t *TryBlock = &Descriptor->TryBlockTable[i];

		/* Sanitize that this tryblock is valid 
		 * for our depth */
		if (TryLevel < TryBlock->StartLevel) 
			continue;
		if (TryLevel > TryBlock->EndLevel) 
			continue;

		/* Ok, we have a valid try block 
		 * now iterate all it's catch blocks */
		for (j = 0; j < TryBlock->CatchBlockCount; j++)
		{
			/* Retrieve the catch block for this index */
			const CxxCatchBlockInformation_t *CatchBlock = &TryBlock->CatchBlock[j];

			/* Do we have any exception information available to us? */
			if (Info) 
			{
				/* Find if the exception match this catch block 
				 * otherwise skip */
				const CxxTypeInfo_t *Type = CxxFindCaughtType(Info, CatchBlock);
				
				/* Sanity */
				if (!Type)
					continue;

				//TRACE("matched type %p in tryblock %d catchblock %d\n", type, i, j);

				/* Copy the exception to its destination on the stack */
				CxxCopyException(Object, Frame, CatchBlock, Type);
			}
			else
			{
				/* no CXX_EXCEPTION only proceed with a catch(...) block*/
				if (CatchBlock->TypeInfo)
					continue;
				//TRACE("found catch(...) block\n");
			}

			/* Unwind the stack */
			RtlUnwind(&Frame->Frame, 0, Record, 0);
			CxxUnwindLocal(Frame, Descriptor, TryBlock->StartLevel);
			Frame->TryLevel = TryBlock->EndLevel + 1;

			/* call the catch block 
			TRACE("calling catch block %p addr %p ebp %p\n",
				catchblock, catchblock->handler, &frame->ebp); */

			/* setup an exception block for nested exceptions */
			NestedFrame.Frame.ExceptionHandler = 
				(PEXCEPTION_HANDLER)CxxCatchFunctionNestedHandler;
			NestedFrame.PreviousRecord = tData->ExceptionRecord;
			NestedFrame.CxxFrame = Frame;
			NestedFrame.Descriptor = Descriptor;
			NestedFrame.TryLevel = NestedTryLevel + 1;
			NestedFrame.Record = Record;

			CxxPushFrame(&NestedFrame.Frame);
			tData->ExceptionRecord = Record;
			Addr = CxxCallEbp(CatchBlock->Handler, &Frame->Ebp);
			tData->ExceptionRecord = NestedFrame.PreviousRecord;
			CxxPopFrame(&NestedFrame.Frame);

			/* Restore the saved esp */
			((uint32_t*)Frame)[-1] = SaveEsp;

			/* Call the destructor if there is any */
			if (Info && Info->Cleanup)
				CxxCallDestructor(Info->Cleanup, Object);
			//TRACE("done, continuing at %p\n", Addr);

			/* Call the rest of the code of the function */
			CxxContinueCatch(Frame, Addr);
		}
	}
}

/* This is the main handler of exception frames 
 * and calls the catch blocks etc etc */
EXCEPTION_DISPOSITION __cdecl CxxHandleFrame(PEXCEPTION_RECORD Record, CxxExceptionFrame_t *Frame,
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
	CxxCallCatch(Record, Frame, Descriptor, Frame->TryLevel, ExceptType);

	/* Done! */
	return ExceptionContinueSearch;
}

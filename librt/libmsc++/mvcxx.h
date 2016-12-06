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

#ifndef _MOLLENOS_VCXX_H_
#define _MOLLENOS_VCXX_H_

/* Includes */
#include <crtdefs.h>
#include <stdint.h>
#include "mscxx.h"

/* These are primarily function
 * pointer typedefs used by below 
 * structures */
typedef void(*vTablePtr)(void);
typedef void(*CxxCopyContructor)(void);

typedef void(*CxxTerminateHandler)(void);
typedef void(*CxxTerminateFunction)(void);
typedef void(*CxxUnexpectedHandler)(void);
typedef void(*CxxUnexpectedFunction)(void);
typedef void(*CxxSETranslatorFunction)(unsigned int code, struct _EXCEPTION_POINTERS *info);

typedef void* (__cdecl *malloc_func_t)(size_t);
typedef void(__cdecl *free_func_t)(void*);

/* __unDName/__unDNameEx flags */
#define UNDNAME_COMPLETE                 (0x0000)
#define UNDNAME_NO_LEADING_UNDERSCORES   (0x0001) /* Don't show __ in calling convention */
#define UNDNAME_NO_MS_KEYWORDS           (0x0002) /* Don't show calling convention at all */
#define UNDNAME_NO_FUNCTION_RETURNS      (0x0004) /* Don't show function/method return value */
#define UNDNAME_NO_ALLOCATION_MODEL      (0x0008)
#define UNDNAME_NO_ALLOCATION_LANGUAGE   (0x0010)
#define UNDNAME_NO_MS_THISTYPE           (0x0020)
#define UNDNAME_NO_CV_THISTYPE           (0x0040)
#define UNDNAME_NO_THISTYPE              (0x0060)
#define UNDNAME_NO_ACCESS_SPECIFIERS     (0x0080) /* Don't show access specifier (public/protected/private) */
#define UNDNAME_NO_THROW_SIGNATURES      (0x0100)
#define UNDNAME_NO_MEMBER_TYPE           (0x0200) /* Don't show static/virtual specifier */
#define UNDNAME_NO_RETURN_UDT_MODEL      (0x0400)
#define UNDNAME_32_BIT_DECODE            (0x0800)
#define UNDNAME_NAME_ONLY                (0x1000) /* Only report the variable/method name */
#define UNDNAME_NO_ARGUMENTS             (0x2000) /* Don't show method arguments */
#define UNDNAME_NO_SPECIAL_SYMS          (0x4000)
#define UNDNAME_NO_COMPLEX_TYPE          (0x8000)

/* Magic numbers used by VC++ */
#define CXX_FRAME_MAGIC_VC6 0x19930520
#define CXX_FRAME_MAGIC_VC7 0x19930521
#define CXX_FRAME_MAGIC_VC8 0x19930522

/* Visual studio uses the magic number 
 * below in their exceptions */
#define CXX_EXCEPTION       0xe06d7363

/* EH Macros */
#define EH_NONCONTINUABLE   0x01
#define EH_UNWINDING        0x02
#define EH_EXIT_UNWIND      0x04
#define EH_STACK_INVALID    0x08
#define EH_NESTED_CALL      0x10

/* Access the internal structure of
 * the type_info object */
typedef struct __InternalTypeInfo
{
	/* The vTable pointer */
	const vTablePtr		*vTable;

	/* Unmangled name, allocated lazily */
	char				*Name;

	/* Variable length, but we declare it large enough for static RTTI */
	char               Mangled[32];

} InternalTypeInfo_t;

/* Access the internal structure of
 * the exception object */
typedef struct __InternalException
{
	/* The vTable pointer */
	const vTablePtr		*vTable;

	/* Name of this exception
	* there is a copy for each class */
	char				*ExcName;

	/* Wheter or not we should
	* free the above name */
	int					 FreeName;

} InternalException_t;

/* The exception frame used by CxxFrameHandler */
typedef struct __CxxExceptionFrame
{
	/* The standard exception frame used
	 * by this struct */
	EXCEPTION_REGISTRATION_RECORD  Frame;

	/* The level of nesting we are in */
	int                            TryLevel;

	/* Our frame pointer */
	uint32_t                       Ebp;

} CxxExceptionFrame_t;

/* Info about a single catch {} block */
typedef struct __CxxCatchBlockInformation
{
	/* flags (see below) */
	uint32_t         Flags;

	/* C++ type information about 
	 * this catch block */
	const InternalTypeInfo_t *TypeInfo;

	/* Stack offset to copy exception object to */
	int              Offset;

	/* Catch block handler code */
	void(*Handler)(void);

} CxxCatchBlockInformation_t;

#define TYPE_FLAG_CONST      1
#define TYPE_FLAG_VOLATILE   2
#define TYPE_FLAG_REFERENCE  8

/* Information about a single try {} block */
typedef struct __CxxTryBlockInformation
{
	/* Start trylevel of this block */
	int                    StartLevel;

	/* End trylevel of that block */
	int                    EndLevel;

	/* Initial trylevel of the catch block */
	int                    CatchLevel;

	/* Count of catch blocks in array */
	int                    CatchBlockCount;
	
	/* Array of catch blocks */
	const CxxCatchBlockInformation_t *CatchBlock;

} CxxTryBlockInformation_t;

/* info about the unwind handler for a given trylevel */
typedef struct __CxxUnwindInformation
{
	/* Previous nested trylevel unwind handler, 
	 * that we have to run after this one */
	int    PreviousTryLevel;

	/* The unwind handler associated with 
	 * this block */
	void(*Handler)(void);

} CxxUnwindInformation_t;

/* Descriptor of all try blocks of a given function
 * This is the actual descriptor object */
typedef struct __CxxFunctionDescriptor
{
	/* The header magic for this c++ exception
	 * Must be CXX_FRAME_MAGIC */
	uint32_t             Magic;

	/* Number of unwind handlers */
	uint32_t             UnwindCount;

	/* Array of unwind handlers */
	const CxxUnwindInformation_t   *UnwindTable;

	/* Number of try blocks */
	uint32_t             TryBlockCount;

	/* Array of try blocks */
	const CxxTryBlockInformation_t *TryBlockTable;

	/* Ip Map */
	uint32_t             ipmap_count;
	const void          *ipmap;

	/* Expected exceptions list when magic >= VC7 */
	const void          *ExpectedList;

	/* Flags when magic >= VC8 */
	uint32_t             Flags;

} CxxFunctionDescriptor_t;

/* Synchronous exceptions only (built with /EHs) */
#define FUNC_DESCR_SYNCHRONOUS  1

/* offsets for computing the this pointer */
typedef struct __CxxPtrOffsets
{
	/* Offset of the base class
	* this pointer from start of object */
	int         ThisOffset;

	/* Offset of the virtual base
	* class descriptor */
	int         vBaseDescriptor;

	/* Offset of this pointers offset
	* in virtual base class descriptor */
	int         vBaseOffset;

} CxxThisPtrOffsets_t;

/* Complete information about a C++ type */
typedef struct __CxxTypeInfo
{
	/* Flags
	* - See CLASS definitions below */
	uint32_t			 Flags;

	/* C++ Type Info
	* Descripes C++ type information
	* about the object */
	const InternalTypeInfo_t		*TypeInfo;

	/* Offsets for computing the this pointer */
	CxxThisPtrOffsets_t	 Offsets;

	/* Size of object */
	unsigned int		 ObjectSize;

	/* Copy constructor function
	* for the class */
	CxxCopyContructor    Copy;

} CxxTypeInfo_t;

/* Class Flags
* Those are incomplete */
#define CLASS_IS_SIMPLE_TYPE          0x1
#define CLASS_HAS_VIRTUAL_BASE_CLASS  0x4

/* Table of C++ types that apply for a given object */
typedef struct __CxxTypeInfoTable
{
	/* The number of types in the below
	 * table */
	uint32_t             EntryCount;

	/* variable length, we declare it
	 * large enough for static RTTI */
	const CxxTypeInfo_t *Table[3];

} CxxTypeInfoTable_t;

/* Custom cxx exception handler 
 * fingerprint */
typedef EXCEPTION_DISPOSITION(*CxxCustomHandler)(PEXCEPTION_RECORD, CxxExceptionFrame_t*,
	PCONTEXT, EXCEPTION_REGISTRATION_RECORD**, const CxxFunctionDescriptor_t*, int,
	EXCEPTION_REGISTRATION_RECORD *, uint32_t);

/* Type information for an exception object
* this the format that VC++ uses to describe */
typedef struct __CxxExceptionType
{
	/* TYPE_FLAG Flags */
	uint32_t                    Flags;

	/* The class destructor for the
	* exception */
	void(*Cleanup)(void);

	/* Custom handler for this exception */
	CxxCustomHandler			 CustomerHandler;

	/* List of types for this exception object
	* Used for determining which types this exception is */
	const CxxTypeInfoTable_t	*TypeTable;

} CxxExceptionType_t;

CxxTerminateFunction _set_terminate(CxxTerminateFunction func);
CxxUnexpectedFunction _set_unexpected(CxxUnexpectedFunction func);
CxxSETranslatorFunction _set_se_translator(CxxSETranslatorFunction func);

/* Pushes a new exception frame onto the exception list
* this list is local for each thread */
EXCEPTION_REGISTRATION_RECORD *CxxPushFrame(EXCEPTION_REGISTRATION_RECORD *Frame);

/* Pops an exception frame from the exception list
* this list is local for each thread */
EXCEPTION_REGISTRATION_RECORD *CxxPopFrame(EXCEPTION_REGISTRATION_RECORD *Frame);

/* RTL Functions, this include 
 * Unwind, RaiseException etc */
void RtlUnwind(PEXCEPTION_REGISTRATION_RECORD EndFrame, void *Eip,
	PEXCEPTION_RECORD Record, uint32_t ReturnValue);
void RtlRaiseException(PEXCEPTION_RECORD ExceptionRecord);
void RtlRaiseStatus(uint32_t Status);
int RtlDispatchException(PEXCEPTION_RECORD ExceptionRecord, PCONTEXT Context);
int RtlCallVectoredExceptionHandlers(PEXCEPTION_RECORD ExceptionRecord, PCONTEXT Context);
void RtlCallVectoredContinueHandlers(PEXCEPTION_RECORD ExceptionRecord, PCONTEXT Context);
void RtlCaptureContext(PCONTEXT ContextRecord);
void RtlpCaptureContext(PCONTEXT ContextRecord);

/* LowLevel Exception Functions */
_MOS_API uint32_t ZwContinue(PCONTEXT Context, int TestAlert);
_MOS_API uint32_t ZwRaiseException(PEXCEPTION_RECORD ExceptionRecord, PCONTEXT Context, int FirstChance);

#endif //!_MOLLENOS_VCXX_H_
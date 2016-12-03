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

#ifndef _VISUAL_CXX_H_
#define _VISUAL_CXX_H_

/* Includes */
#include <crtdefs.h>
#include <stdint.h>

#ifdef MOLLENOS
#ifndef _VCXXABI_IMPLEMENTATION
#define _CRTXX_ABI __declspec(dllimport)
#else
#define _CRTXX_ABI __declspec(dllexport)
#endif
#endif

/* Definitions */
#define EXCEPTION_MAXIMUM_PARAMETERS   15

/* Exception Flags */
#define EXCEPTION_ACCESS_VIOLATION ((uint32_t)0xC0000005)
#define EXCEPTION_DATATYPE_MISALIGNMENT ((uint32_t)0x80000002)
#define EXCEPTION_BREAKPOINT ((uint32_t)0x80000003)
#define EXCEPTION_SINGLE_STEP ((uint32_t)0x80000004)
#define EXCEPTION_ARRAY_BOUNDS_EXCEEDED ((uint32_t)0xC000008C)
#define EXCEPTION_FLT_DENORMAL_OPERAND ((uint32_t)0xC000008D)
#define EXCEPTION_FLT_DIVIDE_BY_ZERO ((uint32_t)0xC000008E)
#define EXCEPTION_FLT_INEXACT_RESULT ((uint32_t)0xC000008F)
#define EXCEPTION_FLT_INVALID_OPERATION ((uint32_t)0xC0000090)
#define EXCEPTION_FLT_OVERFLOW ((uint32_t)0xC0000091)
#define EXCEPTION_FLT_STACK_CHECK ((uint32_t)0xC0000092)
#define EXCEPTION_FLT_UNDERFLOW ((uint32_t)0xC0000093)
#define EXCEPTION_INT_DIVIDE_BY_ZERO ((uint32_t)0xC0000094)
#define EXCEPTION_INT_OVERFLOW ((uint32_t)0xC0000095)
#define EXCEPTION_PRIV_INSTRUCTION ((uint32_t)0xC0000096)
#define EXCEPTION_IN_PAGE_ERROR ((uint32_t)0xC0000006)
#define EXCEPTION_ILLEGAL_INSTRUCTION ((uint32_t)0xC000001D)
#define EXCEPTION_NONCONTINUABLE_EXCEPTION ((uint32_t)0xC0000025)
#define EXCEPTION_STACK_OVERFLOW ((uint32_t)0xC00000FD)
#define EXCEPTION_INVALID_DISPOSITION ((uint32_t)0xC0000026)
#define EXCEPTION_STATUS_UNWIND ((uint32_t)0xC0000027)
#define EXCEPTION_INVALID_STACK ((uint32_t)0xC0000028)
#define EXCEPTION_INVALID_UNWIND ((uint32_t)0xC0000029)
#define EXCEPTION_GUARD_PAGE ((uint32_t)0x80000001)
#define EXCEPTION_INVALID_HANDLE ((uint32_t)0xC0000008L)

/* Exception record flags */
#define EXCEPTION_NONCONTINUABLE  0x01
#define EXCEPTION_UNWINDING       0x02
#define EXCEPTION_EXIT_UNWIND     0x04
#define EXCEPTION_STACK_INVALID   0x08
#define EXCEPTION_NESTED_CALL     0x10
#define EXCEPTION_TARGET_UNWIND   0x20
#define EXCEPTION_COLLIDED_UNWIND 0x40
#define EXCEPTION_UNWIND (EXCEPTION_UNWINDING | EXCEPTION_EXIT_UNWIND | \
                          EXCEPTION_TARGET_UNWIND | EXCEPTION_COLLIDED_UNWIND)

#define IS_UNWINDING(Flag) ((Flag & EXCEPTION_UNWIND) != 0)
#define IS_DISPATCHING(Flag) ((Flag & EXCEPTION_UNWIND) == 0)
#define IS_TARGET_UNWIND(Flag) (Flag & EXCEPTION_TARGET_UNWIND)

#define EXCEPTION_CHAIN_END ((PEXCEPTION_REGISTRATION_RECORD)-1)

/* Describes an exception record, used
 * to cast and catch exceptions in the lower
 * levels 
 * -- https://msdn.microsoft.com/en-us/library/windows/desktop/aa363082(v=vs.85).aspx */
typedef struct _EXCEPTION_RECORD {

	/* Possible error codes you can 
	 * see in EXCEPTION_ above */
	uint32_t ExceptionCode;

	/* Possible flags you can see 
	 * above in EXCEPTION_ above */
	uint32_t ExceptionFlags;
	struct _EXCEPTION_RECORD *ExceptionRecord;
	void *ExceptionAddress;
	uint32_t NumberParameters;
	uintptr_t ExceptionInformation[EXCEPTION_MAXIMUM_PARAMETERS];
} EXCEPTION_RECORD, *PEXCEPTION_RECORD;

#if defined (_X86_32)
#define SIZE_OF_80387_REGISTERS	80
#define CONTEXT_i386	0x10000
#define CONTEXT_i486	0x10000
#define CONTEXT_CONTROL	(CONTEXT_i386|0x00000001L)
#define CONTEXT_INTEGER	(CONTEXT_i386|0x00000002L)
#define CONTEXT_SEGMENTS	(CONTEXT_i386|0x00000004L)
#define CONTEXT_FLOATING_POINT	(CONTEXT_i386|0x00000008L)
#define CONTEXT_DEBUG_REGISTERS	(CONTEXT_i386|0x00000010L)
#define CONTEXT_EXTENDED_REGISTERS (CONTEXT_i386|0x00000020L)
#define CONTEXT_FULL	(CONTEXT_CONTROL|CONTEXT_INTEGER|CONTEXT_SEGMENTS)
#define MAXIMUM_SUPPORTED_EXTENSION  512

#define EXCEPTION_READ_FAULT    0
#define EXCEPTION_WRITE_FAULT   1
#define EXCEPTION_EXECUTE_FAULT 8

/* User Context 
 * Describes the current context
 * in an exception 
 * -- WinNT.h Line 1540 */
typedef struct _FLOATING_SAVE_AREA {
	uint32_t ControlWord;
	uint32_t StatusWord;
	uint32_t TagWord;
	uint32_t ErrorOffset;
	uint32_t ErrorSelector;
	uint32_t DataOffset;
	uint32_t DataSelector;
	uint8_t RegisterArea[SIZE_OF_80387_REGISTERS];
	uint32_t Cr0NpxState;
} FLOATING_SAVE_AREA, *PFLOATING_SAVE_AREA;

/* User Context 
 * Describes the current context
 * in an exception 
 * -- WinNT.h Line 1540 */
typedef struct _CONTEXT {
	uint32_t ContextFlags;
	uint32_t Dr0;
	uint32_t Dr1;
	uint32_t Dr2;
	uint32_t Dr3;
	uint32_t Dr6;
	uint32_t Dr7;
	FLOATING_SAVE_AREA FloatSave;
	uint32_t SegGs;
	uint32_t SegFs;
	uint32_t SegEs;
	uint32_t SegDs;
	uint32_t Edi;
	uint32_t Esi;
	uint32_t Ebx;
	uint32_t Edx;
	uint32_t Ecx;
	uint32_t Eax;
	uint32_t Ebp;
	uint32_t Eip;
	uint32_t SegCs;
	uint32_t EFlags;
	uint32_t Esp;
	uint32_t SegSs;
	uint8_t ExtendedRegisters[MAXIMUM_SUPPORTED_EXTENSION];
} CONTEXT, *PCONTEXT;
#elif defined(_X86_64)
typedef DECLSPEC_ALIGN(16) struct _M128A {
	unsigned long long Low;
	long long High;
  } M128A, *PM128A;

typedef struct _XMM_SAVE_AREA32 {
	uint16_t ControlWord;
	uint16_t StatusWord;
	uint8_t TagWord;
	uint8_t Reserved1;
	uint16_t ErrorOpcode;
	uint32_t ErrorOffset;
	uint16_t ErrorSelector;
	uint16_t Reserved2;
	uint32_t DataOffset;
	uint16_t DataSelector;
	uint16_t Reserved3;
	uint32_t MxCsr;
	uint32_t MxCsr_Mask;
	M128A FloatRegisters[8];
	M128A XmmRegisters[16];
	uint8_t Reserved4[96];
} XMM_SAVE_AREA32, *PXMM_SAVE_AREA32;

#define LEGACY_SAVE_AREA_LENGTH sizeof(XMM_SAVE_AREA32)

typedef DECLSPEC_ALIGN(16) struct _CONTEXT {
	uint64_t P1Home;
	uint64_t P2Home;
	uint64_t P3Home;
	uint64_t P4Home;
	uint64_t P5Home;
	uint64_t P6Home;
	uint32_t ContextFlags;
	uint32_t MxCsr;
	uint16_t SegCs;
	uint16_t SegDs;
	uint16_t SegEs;
	uint16_t SegFs;
	uint16_t SegGs;
	uint16_t SegSs;
	uint32_t EFlags;
	uint64_t Dr0;
	uint64_t Dr1;
	uint64_t Dr2;
	uint64_t Dr3;
	uint64_t Dr6;
	uint64_t Dr7;
	uint64_t Rax;
	uint64_t Rcx;
	uint64_t Rdx;
	uint64_t Rbx;
	uint64_t Rsp;
	uint64_t Rbp;
	uint64_t Rsi;
	uint64_t Rdi;
	uint64_t R8;
	uint64_t R9;
	uint64_t R10;
	uint64_t R11;
	uint64_t R12;
	uint64_t R13;
	uint64_t R14;
	uint64_t R15;
	uint64_t Rip;
	union {
		XMM_SAVE_AREA32 FltSave;
		XMM_SAVE_AREA32 FloatSave;
		struct {
			M128A Header[2];
			M128A Legacy[8];
			M128A Xmm0;
			M128A Xmm1;
			M128A Xmm2;
			M128A Xmm3;
			M128A Xmm4;
			M128A Xmm5;
			M128A Xmm6;
			M128A Xmm7;
			M128A Xmm8;
			M128A Xmm9;
			M128A Xmm10;
			M128A Xmm11;
			M128A Xmm12;
			M128A Xmm13;
			M128A Xmm14;
			M128A Xmm15;
		};
	};
	M128A VectorRegister[26];
	uint64_t VectorControl;
	uint64_t DebugControl;
	uint64_t LastBranchToRip;
	uint64_t LastBranchFromRip;
	uint64_t LastExceptionToRip;
	uint64_t LastExceptionFromRip;
} CONTEXT, *PCONTEXT;
#else
#error "Undefined Architecture"
#endif

typedef enum _EXCEPTION_DISPOSITION
{
	ExceptionContinueExecution,
	ExceptionContinueSearch,
	ExceptionNestedException,
	ExceptionCollidedUnwind

} EXCEPTION_DISPOSITION;

#define EXCEPTION_EXECUTE_HANDLER 1
#define EXCEPTION_CONTINUE_SEARCH 0
#define EXCEPTION_CONTINUE_EXECUTION -1

/* Typedef for the exception handler function
 * prototype */
typedef EXCEPTION_DISPOSITION(*PEXCEPTION_HANDLER)(
	struct _EXCEPTION_RECORD *ExceptionRecord, 
	struct _EXCEPTION_REGISTRATION_RECORD *EstablisherFrame,
	struct _CONTEXT *ContextRecord,
	struct _EXCEPTION_REGISTRATION_RECORD **DispatcherContext);

/* Definition of 'raw' WinNt exception
 * registration record - this ought to be in WinNt.h */
typedef struct _EXCEPTION_REGISTRATION_RECORD {
	struct _EXCEPTION_REGISTRATION_RECORD *NextRecord;
	PEXCEPTION_HANDLER ExceptionHandler;
} EXCEPTION_REGISTRATION_RECORD, *PEXCEPTION_REGISTRATION_RECORD;

/* Describes an exception record, used
 * to cast and catch exceptions in the lower
 * levels 
 * -- https://msdn.microsoft.com/en-us/library/windows/desktop/aa363082(v=vs.85).aspx */
typedef struct _EXCEPTION_RECORD32 {
	int ExceptionCode;
	uint32_t ExceptionFlags;
	uint32_t ExceptionRecord;
	uint32_t ExceptionAddress;
	uint32_t NumberParameters;
	uint32_t ExceptionInformation[EXCEPTION_MAXIMUM_PARAMETERS];
} EXCEPTION_RECORD32, *PEXCEPTION_RECORD32;

/* Describes an exception record, used
 * to cast and catch exceptions in the lower
 * levels 
 * -- https://msdn.microsoft.com/en-us/library/windows/desktop/aa363082(v=vs.85).aspx */
typedef struct _EXCEPTION_RECORD64 {
	int ExceptionCode;
	uint32_t ExceptionFlags;
	uint64_t ExceptionRecord;
	uint64_t ExceptionAddress;
	uint32_t NumberParameters;
	uint32_t __unusedAlignment;
	uint64_t ExceptionInformation[EXCEPTION_MAXIMUM_PARAMETERS];
} EXCEPTION_RECORD64, *PEXCEPTION_RECORD64;

/* Contains an exception record with a machine-independent 
 * description of an exception and a context record with a machine-dependent 
 * description of the processor context at the time of the exception. 
 * -- https://msdn.microsoft.com/en-us/library/windows/desktop/ms679331(v=vs.85).aspx */
typedef struct _EXCEPTION_POINTERS {
	PEXCEPTION_RECORD ExceptionRecord;
	PCONTEXT ContextRecord;
} EXCEPTION_POINTERS, *PEXCEPTION_POINTERS;

#endif
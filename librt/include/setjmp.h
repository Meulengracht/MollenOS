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
* MollenOS C Library - SetJmp & LongJmp
*/

#ifndef __SETJMP_INC__
#define __SETJMP_INC__

/* Includes */
#include <crtdefs.h>
#include <stdint.h>

#pragma pack(push, _CRT_PACKING)

#ifdef __cplusplus
extern "C" {
#endif

#if (defined(_X86_) && !defined(__x86_64)) || defined(_X86_32) || defined(i386)

#define _JBLEN 16
#define _JBTYPE int

typedef struct __JUMP_BUFFER {
	unsigned long Ebp;
	unsigned long Ebx;
	unsigned long Edi;
	unsigned long Esi;
	unsigned long Esp;
	unsigned long Eip;
	unsigned long Registration;
	unsigned long TryLevel;
	unsigned long Cookie;
	unsigned long UnwindFunc;
	unsigned long UnwindData[6];
} _JUMP_BUFFER;

#elif defined(__ia64__)

	typedef _CRT_ALIGN(16) struct _SETJMP_FLOAT128 {
		__int64 LowPart;
		__int64 HighPart;
	} SETJMP_FLOAT128;

#define _JBLEN 33
	typedef SETJMP_FLOAT128 _JBTYPE;

	typedef struct __JUMP_BUFFER {

		unsigned long iAReserved[6];

		unsigned long Registration;
		unsigned long TryLevel;
		unsigned long Cookie;
		unsigned long UnwindFunc;

		unsigned long UnwindData[6];

		SETJMP_FLOAT128 FltS0;
		SETJMP_FLOAT128 FltS1;
		SETJMP_FLOAT128 FltS2;
		SETJMP_FLOAT128 FltS3;
		SETJMP_FLOAT128 FltS4;
		SETJMP_FLOAT128 FltS5;
		SETJMP_FLOAT128 FltS6;
		SETJMP_FLOAT128 FltS7;
		SETJMP_FLOAT128 FltS8;
		SETJMP_FLOAT128 FltS9;
		SETJMP_FLOAT128 FltS10;
		SETJMP_FLOAT128 FltS11;
		SETJMP_FLOAT128 FltS12;
		SETJMP_FLOAT128 FltS13;
		SETJMP_FLOAT128 FltS14;
		SETJMP_FLOAT128 FltS15;
		SETJMP_FLOAT128 FltS16;
		SETJMP_FLOAT128 FltS17;
		SETJMP_FLOAT128 FltS18;
		SETJMP_FLOAT128 FltS19;
		__int64 FPSR;
		__int64 StIIP;
		__int64 BrS0;
		__int64 BrS1;
		__int64 BrS2;
		__int64 BrS3;
		__int64 BrS4;
		__int64 IntS0;
		__int64 IntS1;
		__int64 IntS2;
		__int64 IntS3;
		__int64 RsBSP;
		__int64 RsPFS;
		__int64 ApUNAT;
		__int64 ApLC;
		__int64 IntSp;
		__int64 IntNats;
		__int64 Preds;

	} _JUMP_BUFFER;

#elif defined(_x86_64)

	typedef _CRT_ALIGN(16) struct _SETJMP_FLOAT128 {
		unsigned __int64 Part[2];
	} SETJMP_FLOAT128;

#define _JBLEN 16
	typedef SETJMP_FLOAT128 _JBTYPE;

	typedef struct _JUMP_BUFFER {
		unsigned __int64 Frame;
		unsigned __int64 Rbx;
		unsigned __int64 Rsp;
		unsigned __int64 Rbp;
		unsigned __int64 Rsi;
		unsigned __int64 Rdi;
		unsigned __int64 R12;
		unsigned __int64 R13;
		unsigned __int64 R14;
		unsigned __int64 R15;
		unsigned __int64 Rip;
		unsigned __int64 Spare;
		SETJMP_FLOAT128 Xmm6;
		SETJMP_FLOAT128 Xmm7;
		SETJMP_FLOAT128 Xmm8;
		SETJMP_FLOAT128 Xmm9;
		SETJMP_FLOAT128 Xmm10;
		SETJMP_FLOAT128 Xmm11;
		SETJMP_FLOAT128 Xmm12;
		SETJMP_FLOAT128 Xmm13;
		SETJMP_FLOAT128 Xmm14;
		SETJMP_FLOAT128 Xmm15;
	} _JUMP_BUFFER;

#elif defined(_M_ARM)

#define _JBLEN  28
#define _JBTYPE int

	typedef struct _JUMP_BUFFER {
		unsigned long Frame;
		unsigned long R4;
		unsigned long R5;
		unsigned long R6;
		unsigned long R7;
		unsigned long R8;
		unsigned long R9;
		unsigned long R10;
		unsigned long R11;
		unsigned long Sp;
		unsigned long Pc;
		unsigned long Fpscr;
		unsigned long long D[8]; // D8-D15 VFP/NEON regs
	} _JUMP_BUFFER;

#else

#error Define Setjmp for this architecture!

#endif

/* Definitions */
#ifndef _JMP_BUF_DEFINED
typedef _JBTYPE jmp_buf[_JBLEN];
#define _JMP_BUF_DEFINED
#endif

/* Prototypes */

/* The save in time -> jump */
_CRTIMP int _setjmp(jmp_buf env);
_CRTIMP int _setjmp3(jmp_buf env, int nb_args, ...);

/* Shorthand */
#define setjmp(env) _setjmp(env)

/* Restore time-state -> jmp */
_CRTIMP void longjmp(jmp_buf env, int value);

#ifdef __cplusplus
}
#endif

#pragma pack(pop, _CRT_PACKING)

#endif //!__SETJMP_INC__

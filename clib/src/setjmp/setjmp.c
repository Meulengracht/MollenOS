/* Saves registers & stack pointer & ip
 *
 */

#include <setjmp.h>

#define OFS_EAX   0
#define OFS_EBX   4
#define OFS_ECX   8
#define OFS_EDX   12
#define OFS_ESI   16
#define OFS_EDI   20
#define OFS_EBP   24
#define OFS_ESP   28
#define OFS_EIP   32

int setjmp(jmp_buf env) 
{
	__asm {
		//Save edi
		mov		edi, 8[ebp]

		//Save stuff
		mov		OFS_EAX[edi], eax
		mov		OFS_EBX[edi], ebx
		mov		OFS_ECX[edi], ecx
		mov		OFS_EDX[edi], edx
		mov		OFS_ESI[edi], esi

		//Get EDI
		mov		eax, [ebp-4]
		mov		OFS_EDI[edi], eax

		//Get EBP
		mov		eax, 0[ebp]
		mov		OFS_EBP[edi], eax

		mov		eax, esp
		add		eax, 12
		mov		OFS_ESP[edi], eax

		mov		eax, 4[ebp]
		mov		OFS_EIP[edi], eax
	}

	return 0;
}

//frame pointer modified
#pragma warning(disable:4731)

void longjmp(jmp_buf env, int value) 
{
	__asm {
		
		/* get jmp_buf */
		mov		edi, 8[ebp]	
		
		/* store retval in j->eax */
		mov		eax, 12[ebp]
		test	eax,eax
		jne		s1
		inc		eax

s1:
		mov		0[edi], eax

		mov		ebp, OFS_EBP[edi]

		//cli
		mov		esp, OFS_ESP[edi]

		push	OFS_EIP[edi]	

		mov		eax, OFS_EAX[edi]
		mov		ebx, OFS_EBX[edi]
		mov		ecx, OFS_ECX[edi]
		mov		edx, OFS_EDX[edi]
		mov		esi, OFS_ESI[edi]
		mov		edi, OFS_EDI[edi]
		//sti

		ret
	}
}

#pragma warning(default:4731)
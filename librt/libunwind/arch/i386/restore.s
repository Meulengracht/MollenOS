default	rel
%define XMMWORD
%define YMMWORD
%define ZMMWORD

section	.text code align=64

global	_ZN9libunwind13Registers_x866jumptoEv
	mov	eax,ecx
	mov	edx,DWORD[28+eax]
	sub	edx,8
	mov	DWORD[28+eax],edx
	mov	ebx,DWORD[eax]
	mov	DWORD[rdx],ebx
	mov	ebx,DWORD[40+eax]
	mov	DWORD[4+rdx],ebx

	mov	ebx,DWORD[4+eax]
	mov	ecx,DWORD[8+eax]
	mov	edx,DWORD[12+eax]
	mov	edi,DWORD[16+eax]
	mov	esi,DWORD[20+eax]
	mov	ebp,DWORD[24+eax]
	mov	esp,DWORD[28+eax]

	pop	eax
	DB	0F3h,0C3h		;repret

section	.note.GNU-stack

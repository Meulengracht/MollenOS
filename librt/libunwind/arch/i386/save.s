default	rel
%define XMMWORD
%define YMMWORD
%define ZMMWORD

section	.text code align=64
global	unw_getcontext
	push	eax
	mov	eax,DWORD[8+esp]
	mov	DWORD[4+eax],ebx
	mov	DWORD[8+eax],ecx
	mov	DWORD[12+eax],edx
	mov	DWORD[16+eax],edi
	mov	DWORD[20+eax],esi
	mov	DWORD[24+eax],ebp
	mov	edx,esp
	add	edx,8
	mov	DWORD[28+eax],edx

	mov	edx,DWORD[4+esp]
	mov	DWORD[40+eax],edx

	mov	edx,DWORD[esp]
	mov	DWORD[eax],edx
	pop	eax
	xor	eax,eax
	DB	0F3h,0C3h		;repret

section	.note.GNU-stack

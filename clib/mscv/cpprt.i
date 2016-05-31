








































































































































































































.intel_syntax noprefix

.altmacro















.macro .PROC name
    .func \name




    \name:
    .cfi_startproc
    .equ cfa_current_offset, -8
.endm



.macro .ENDP
    .cfi_endproc
    .endfunc
.endm



.macro PUBLIC symbol
    .global \symbol
.endm


.macro GLOBAL_LABEL label
    \label:
.endm


.macro ASSUME p1 p2 p3 p4 p5 p6 p7 p8
.endm


.macro .endcode16
.endm








.macro ljmp segment, offset
    jmp far ptr \segment:\offset
.endm

.macro ljmp16 segment, offset
    jmp far ptr \segment:\offset
.endm


.macro EXTERN name
.endm




.macro .MODEL model
.endm

.macro .code
    .text
.endm



.macro FPO cdwLocals, cdwParams, cbProlog, cbRegs, fUseBP, cbFrame
    
.endm



.macro .allocstack size
    .cfi_adjust_cfa_offset \size
    .set cfa_current_offset, cfa_current_offset - \size
.endm

code = 1
.macro .pushframe param=0
    .if (\param)
        .cfi_adjust_cfa_offset 0x30
        .set cfa_current_offset, cfa_current_offset - 0x30
    .else
        .cfi_adjust_cfa_offset 0x28
        .set cfa_current_offset, cfa_current_offset - 0x28
    .endif
.endm

.macro .pushreg reg
    .cfi_adjust_cfa_offset 8
    .equ cfa_current_offset, cfa_current_offset - 8
    .cfi_offset \reg, cfa_current_offset
.endm

.macro .savereg reg, offset
    
    .cfi_offset \reg, \offset
.endm

.macro .savexmm128 reg, offset
    
    .cfi_offset \reg, \offset
.endm

.macro .setframe reg, offset
    .cfi_def_cfa reg, \offset
    .equ cfa_current_offset, \offset
.endm

.macro .endprolog
.endm

.macro absolute address
    __absolute__address__ = \address
.endm

.macro resb name, size
    \name = __absolute__address__
    __absolute__address__ = __absolute__address__ + \size
.endm

.macro UNIMPLEMENTED2 file, line, func
    jmp 3f
1:  .asciz "\func"
2:  .asciz \file
3:
    sub rsp, 0x20
    lea rcx, MsgUnimplemented[rip]
    lea rdx, 1b[rip]
    lea r8, 2b[rip]
    mov r9, \line
    call DbgPrint
    add rsp, 0x20
.endm

























.code

.macro DEFINE_ALIAS, alias, orig, type
EXTERN &orig:&type
ALIAS <&alias> = <&orig>
.endm

EXTERN _CxxHandleV8Frame@20 : PROC
PUBLIC ___CxxFrameHandler3
___CxxFrameHandler3:
    push eax
    push dword ptr [esp + 20]
    push dword ptr [esp + 20]
    push dword ptr [esp + 20]
    push dword ptr [esp + 20]
    call _CxxHandleV8Frame@20
    ret

EXTERN ___CxxFrameHandler : PROC
PUBLIC _CallCxxFrameHandler
_CallCxxFrameHandler:
    mov eax, dword ptr [esp + 20]
    jmp ___CxxFrameHandler

; void __stdcall `eh vector constructor iterator'(void *,unsigned int,int,void (__thiscall*)(void *),void (__thiscall*)(void *))
DEFINE_ALIAS ??_L@YGXPAXIHP6EX0@Z1@Z, ?MSVCRTEX_eh_vector_constructor_iterator@@YGXPAXIHP6EX0@Z1@Z

; void __stdcall `eh vector constructor iterator'(void *,unsigned int,unsigned int,void (__thiscall*)(void *),void (__thiscall*)(void *))
DEFINE_ALIAS ??_L@YGXPAXIIP6EX0@Z1@Z, ?MSVCRTEX_eh_vector_constructor_iterator@@YGXPAXIHP6EX0@Z1@Z

; void __stdcall `eh vector destructor iterator'(void *,unsigned int,int,void (__thiscall*)(void *))
DEFINE_ALIAS ??_M@YGXPAXIHP6EX0@Z@Z, ?MSVCRTEX_eh_vector_destructor_iterator@@YGXPAXIHP6EX0@Z@Z

; void __stdcall `eh vector destructor iterator'(void *,unsigned int,unsigned int,void (__thiscall*)(void *))
DEFINE_ALIAS ??_M@YGXPAXIIP6EX0@Z@Z, ?MSVCRTEX_eh_vector_destructor_iterator@@YGXPAXIHP6EX0@Z@Z

; void __cdecl operator delete(void *,unsigned int)
DEFINE_ALIAS ??3@YAXPAXI@Z, ??3@YAXPAX@Z

; void __cdecl operator delete(void *,struct std::nothrow_t const &)
DEFINE_ALIAS ??3@YAXPAXABUnothrow_t@std@@@Z, ??3@YAXPAX@Z

; void __cdecl operator delete[](void *,struct std::nothrow_t const &)
DEFINE_ALIAS ??_V@YAXPAXABUnothrow_t@std@@@Z, ??3@YAXPAX@Z



;title   llmul - long multiply routine
;***
;llmul.asm - long multiply routine
;
;       Copyright (c) Microsoft Corporation. All rights reserved.
;
;Purpose:
;       Defines long multiply routine
;       Both signed and unsigned routines are the same, since multiply's
;       work out the same in 2's complement
;       creates the following routine:
;           __allmul
;
;*******************************************************************************
bits 32
segment .text

;Functions in this asm 
global __allmul

;***
;llmul - long multiply routine
;
;Purpose:
;       Does a long multiply (same for signed/unsigned)
;       Parameters are not changed.
;
;Entry:
;       Parameters are passed on the stack:
;               1st pushed: multiplier (QWORD)
;               2nd pushed: multiplicand (QWORD)
;
;Exit:
;       EDX:EAX - product of multiplier and multiplicand
;       NOTE: parameters are removed from the stack
;
;Uses:
;       ECX
;
;Exceptions:
;
;*******************************************************************************
__allmul:
%define A     [esp + 4]       ; stack address of dividend (a)
%define AU    [esp + 8]       ; stack address of dividend (a)
%define B     [esp + 12]      ; stack address of divisor (b)
%define BU    [esp + 16]      ; stack address of divisor (b)

;
;       AHI, BHI : upper 32 bits of A and B
;       ALO, BLO : lower 32 bits of A and B
;
;             ALO * BLO
;       ALO * BHI
; +     BLO * AHI
; ---------------------
;

        mov     eax,AU
        mov     ecx,BU
        or      ecx,eax         ;test for both hiwords zero.
        mov     ecx,B
        jnz     short hard      ;both are zero, just mult ALO and BLO

        mov     eax,A
        mul     ecx

        ret     16              ; callee restores the stack

hard:
        push    ebx

; must redefine A and B since esp has been altered
%define A2     [esp + 8]       ; stack address of dividend (a)
%define A2U    [esp + 12]       ; stack address of dividend (a)
%define B2     [esp + 16]      ; stack address of divisor (b)
%define B2U    [esp + 20]      ; stack address of divisor (b)

        mul     ecx             ;eax has AHI, ecx has BLO, so AHI * BLO
        mov     ebx,eax         ;save result

        mov     eax,A2
        mul     dword B2U ;ALO * BHI
        add     ebx,eax         ;ebx = ((ALO * BHI) + (AHI * BLO))

        mov     eax,A2  ;ecx = BLO
        mul     ecx             ;so edx:eax = ALO*BLO
        add     edx,ebx         ;now edx has all the LO*HI stuff

        pop     ebx

        ret     16              ; callee restores the stack

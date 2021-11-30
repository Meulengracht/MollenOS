;------------------------------------------------------------------------------
;
; Copyright (c) 2006 - 2019, Intel Corporation. All rights reserved.<BR>
; SPDX-License-Identifier: BSD-2-Clause-Patent
;
; Module Name:
;
;   SetJump.Asm
;
; Abstract:
;
;   Implementation of SetJump() on IA-32.
;
;------------------------------------------------------------------------------

%include "Nasm.inc"

    SECTION .text

extern _InternalAssertJumpBuffer

;------------------------------------------------------------------------------
; UINTN
; EFIAPI
; SetJump (
;   OUT     BASE_LIBRARY_JUMP_BUFFER  *JumpBuffer
;   );
;------------------------------------------------------------------------------
global _SetJump
_SetJump:
    push    DWORD [esp + 4]
    call    _InternalAssertJumpBuffer    ; To validate JumpBuffer
    pop     ecx
    pop     ecx                         ; ecx <- return address
    mov     edx, [esp]

    xor     eax, eax
    mov     [edx + 24], eax        ; save 0 to SSP

    mov     eax, _PCD_GET_MODE_32_PcdControlFlowEnforcementPropertyMask
    test    eax, eax
    jz      CetDone
    mov     eax, cr4
    bt      eax, 23                ; check if CET is enabled
    jnc     CetDone

    mov     eax, 1
    INCSSP_EAX                     ; to read original SSP
    READSSP_EAX
    mov     [edx + 0x24], eax      ; save SSP

CetDone:

    mov     [edx], ebx
    mov     [edx + 4], esi
    mov     [edx + 8], edi
    mov     [edx + 12], ebp
    mov     [edx + 16], esp
    mov     [edx + 20], ecx             ; eip value to restore in LongJump
    xor     eax, eax
    jmp     ecx


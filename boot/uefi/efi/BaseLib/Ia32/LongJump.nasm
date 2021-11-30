;------------------------------------------------------------------------------
;
; Copyright (c) 2006 - 2019, Intel Corporation. All rights reserved.<BR>
; SPDX-License-Identifier: BSD-2-Clause-Patent
;
; Module Name:
;
;   LongJump.Asm
;
; Abstract:
;
;   Implementation of _LongJump() on IA-32.
;
;------------------------------------------------------------------------------

%include "Nasm.inc"

    SECTION .text

;------------------------------------------------------------------------------
; VOID
; EFIAPI
; InternalLongJump (
;   IN      BASE_LIBRARY_JUMP_BUFFER  *JumpBuffer,
;   IN      UINTN                     Value
;   );
;------------------------------------------------------------------------------
global _InternalLongJump
_InternalLongJump:

    mov     eax, _PCD_GET_MODE_32_PcdControlFlowEnforcementPropertyMask
    test    eax, eax
    jz      CetDone
    mov     eax, cr4
    bt      eax, 23                ; check if CET is enabled
    jnc     CetDone

    mov     edx, [esp + 4]         ; edx = JumpBuffer
    mov     edx, [edx + 24]        ; edx = target SSP
    READSSP_EAX
    sub     edx, eax               ; edx = delta
    mov     eax, edx               ; eax = delta

    shr     eax, 2                 ; eax = delta/sizeof(UINT32)
    INCSSP_EAX

CetDone:

    pop     eax                         ; skip return address
    pop     edx                         ; edx <- JumpBuffer
    pop     eax                         ; eax <- Value
    mov     ebx, [edx]
    mov     esi, [edx + 4]
    mov     edi, [edx + 8]
    mov     ebp, [edx + 12]
    mov     esp, [edx + 16]
    jmp     dword [edx + 20]        ; restore "eip"


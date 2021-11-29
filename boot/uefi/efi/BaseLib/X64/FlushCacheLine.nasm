;------------------------------------------------------------------------------
;
; Copyright (c) 2006, Intel Corporation. All rights reserved.<BR>
; SPDX-License-Identifier: BSD-2-Clause-Patent
;
; Module Name:
;
;   FlushCacheLine.Asm
;
; Abstract:
;
;   AsmFlushCacheLine function
;
; Notes:
;
;------------------------------------------------------------------------------

    DEFAULT REL
    SECTION .text

;------------------------------------------------------------------------------
; VOID *
; EFIAPI
; AsmFlushCacheLine (
;   IN      VOID                      *LinearAddress
;   );
;------------------------------------------------------------------------------
global AsmFlushCacheLine
AsmFlushCacheLine:
    clflush [rcx]
    mov     rax, rcx
    ret


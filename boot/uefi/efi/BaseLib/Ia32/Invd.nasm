;------------------------------------------------------------------------------
;
; Copyright (c) 2006 - 2016, Intel Corporation. All rights reserved.<BR>
; SPDX-License-Identifier: BSD-2-Clause-Patent
;
; Module Name:
;
;   Invd.Asm
;
; Abstract:
;
;   AsmInvd function
;
; Notes:
;
;------------------------------------------------------------------------------

    SECTION .text

;------------------------------------------------------------------------------
; VOID
; EFIAPI
; AsmInvd (
;   VOID
;   );
;------------------------------------------------------------------------------
global _AsmInvd
_AsmInvd:
    invd
    ret


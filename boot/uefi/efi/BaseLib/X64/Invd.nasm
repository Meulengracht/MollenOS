;------------------------------------------------------------------------------
;
; Copyright (c) 2006, Intel Corporation. All rights reserved.<BR>
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

    DEFAULT REL
    SECTION .text

;------------------------------------------------------------------------------
; VOID
; EFIAPI
; AsmInvd (
;   VOID
;   );
;------------------------------------------------------------------------------
global AsmInvd
AsmInvd:
    invd
    ret


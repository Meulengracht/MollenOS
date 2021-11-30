;------------------------------------------------------------------------------
;
; Copyright (c) 2006, Intel Corporation. All rights reserved.<BR>
; SPDX-License-Identifier: BSD-2-Clause-Patent
;
; Module Name:
;
;   Wbinvd.Asm
;
; Abstract:
;
;   AsmWbinvd function
;
; Notes:
;
;------------------------------------------------------------------------------

    SECTION .text

;------------------------------------------------------------------------------
; VOID
; EFIAPI
; AsmWbinvd (
;   VOID
;   );
;------------------------------------------------------------------------------
global _AsmWbinvd
_AsmWbinvd:
    wbinvd
    ret


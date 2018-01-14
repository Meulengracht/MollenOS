;***
;dllsupp.asm - Definitions of public constants
;
;       Copyright (c) Microsoft Corporation. All rights reserved.
;
;Purpose:
;       Provides definitions for public constants (absolutes) that are
;       'normally' defined in objects in the C library, but must be defined
;       here for clients of crtdll.dll & msvcrt*.dll.  These constants are:
;
;                           _except_list
;                           _fltused
;                           _ldused
;
;*******************************************************************************
bits 32
SECTION .data

; offset, with respect to FS, of pointer to currently active exception handler.
; referenced by compiler generated code for SEH and by _setjmp().

global _except_list
_except_list    dd     4

global _fltused
_fltused        dd     9876h

global __fltused
__fltused       dd     9876h

global _ldused
_ldused         dd     9876h
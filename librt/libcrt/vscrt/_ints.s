;page    ,132
;title   enable - enable/disable interrupts
;***
;enable.asm - contains _enable() and _disable() routines
;
;       Copyright (c) Microsoft Corporation. All rights reserved.
;
;Purpose:
;
;*******************************************************************************

;page
;***
;void _enable(void)  - enables interrupts
;void _disable(void) - disables interrupts
;
;Purpose:
;       The _enable() functions executes a "sti" instruction. The _disable()
;       function executes a "cli" instruction.
;
;Entry:
;
;Exit:
;
;Uses:
;
;Exceptions:
;
;*******************************************************************************
bits 32
segment .text

;Functions in this asm 
global _enable
global _disable

_enable:
        sti
        ret

_disable:
        cli
        ret

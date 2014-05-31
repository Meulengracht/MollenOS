        page    ,132
        title   enable - enable/disable interrupts
;***
;enable.asm - contains _enable() and _disable() routines
;
;       Copyright (c) Microsoft Corporation. All rights reserved.
;
;Purpose:
;
;*******************************************************************************

page
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

                .386
_TEXT           segment use32 para public 'CODE'
				public  _enable, _disable

_enable proc

        sti
        ret

_enable endp

        align   4

_disable proc

        cli
        ret

_disable endp


_TEXT           ends
                end

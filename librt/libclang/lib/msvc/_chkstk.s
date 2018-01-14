;-----------------------------------------------------------------------------
; chkstk.asm - check stack upon procedure entry
; Adapted from Visual Studio C runtime library
; Portions Copyright (c) Microsoft Corporation. All rights reserved. 
;-----------------------------------------------------------------------------
bits 32
segment .text

;Functions in this asm 
global __chkstk
global __alloca_probe
global __alloca_probe_8
global __alloca_probe_16
         
%define PAGESIZE     4096

__chkstk:
__alloca_probe:
                cmp     eax, PAGESIZE           ; more than one page?
                jae     short probesetup        ;   yes, go setup probe loop
                                                ;   no
                neg     eax                     ; compute new stack pointer in eax
                add     eax,esp
                add     eax,4
                test    dword [eax],eax     ; probe it
                xchg    eax,esp
                mov     eax,dword [eax]
                push    eax
                ret

probesetup:
                push    ecx                     ; save ecx
                lea     ecx,[esp + 8]           ; compute new stack pointer in ecx
                                                ; correct for return address and
                                                ; saved ecx

probepages:
                sub     ecx,PAGESIZE            ; yes, move down a page
                sub     eax,PAGESIZE            ; adjust request and...

                test    dword [ecx],eax     ; ...probe it

                cmp     eax,PAGESIZE            ; more than one page requested?
                jae     short probepages        ; no

lastpage:
                sub     ecx,eax                 ; move stack down by eax
                mov     eax,esp                 ; save current tos and do a...

                test    dword [ecx],eax     ; ...probe in case a page was crossed

                mov     esp,ecx                 ; set the new stack pointer

                mov     ecx,dword [eax]     ; recover ecx
                mov     eax,dword [eax + 4] ; recover return address

                push    eax                     ; prepare return address
                                                ; ...probe in case a page was crossed
                ret

; 16 byte aligned alloca
__alloca_probe_16:
                push    ecx
                lea     ecx, [esp + 8]          ; TOS before entering this function
                sub     ecx, eax                ; New TOS
                and     ecx, (16 - 1)           ; Distance from 16 bit align (align down)
                add     eax, ecx                ; Increase allocation size
                sbb     ecx, ecx                ; ecx = 0xFFFFFFFF if size wrapped around
                or      eax, ecx                ; cap allocation size on wraparound
                pop     ecx                     ; Restore ecx
                jmp     __chkstk

; 8 byte aligned alloca
__alloca_probe_8:
                push    ecx
                lea     ecx, [esp + 8]          ; TOS before entering this function
                sub     ecx, eax                ; New TOS
                and     ecx, (8 - 1)            ; Distance from 8 bit align (align down)
                add     eax, ecx                ; Increase allocation Size
                sbb     ecx, ecx                ; ecx = 0xFFFFFFFF if size wrapped around
                or      eax, ecx                ; cap allocation size on wraparound
                pop     ecx                     ; Restore ecx
                jmp     __chkstk
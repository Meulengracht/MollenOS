; MollenOS
;
; Copyright 2011-2014, Philip Meulengracht
;
; This program is free software: you can redistribute it and/or modify
; it under the terms of the GNU General Public License as published by
; the Free Software Foundation?, either version 3 of the License, or
; (at your option) any later version.
;
; This program is distributed in the hope that it will be useful,
; but WITHOUT ANY WARRANTY; without even the implied warranty of
; MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
; GNU General Public License for more details.
;
; You should have received a copy of the GNU General Public License
; along with this program.  If not, see <http://www.gnu.org/licenses/>.
;
;
; MollenOS x86-32 Spinlock Code
;
.386
.model flat
option casemap :none 

;Functions in this asm
public _spinlock_reset
public _spinlock_acquire
public _spinlock_release

_text segment
; void spinlock_reset(spinlock_t *spinlock)
; We set the spinlock to value 0
_spinlock_reset proc near
	; Stack Frame
	push ebp
	mov ebp, esp
	push ebx

	; Get address of lock
	mov ebx, dword ptr 8[ebp]
	mov dword ptr [ebx], 0

	; Release stack frame
	xor eax, eax
	pop ebx
	pop ebp
	ret 
_spinlock_reset endp
_text ends

_text segment
; int spinlock_acquire(spinlock_t *spinlock)
; We wait for the spinlock to become free
; then set value to 1 to mark it in use.
_spinlock_acquire proc near
	; Stack Frame
	push ebp
	mov ebp, esp
	push ebx

	; Get address of lock
	mov ebx, dword ptr 8[ebp]
	mov eax, 1

	; Try to get lock
	trylock:
	xchg dword ptr [ebx], eax
	test eax, eax
	je gotlock

	; Busy-wait loop
	lockloop:
	pause
	cmp dword ptr [ebx], 0
	jne lockloop
	jmp trylock

	gotlock:
	; Release stack frame
	mov eax, 1
	pop ebx
	pop ebp
	ret
_spinlock_acquire endp
_text ends

_text segment
; void spinlock_release(spinlock_t *spinlock)
; We set the spinlock to value 0
_spinlock_release proc near
	; Stack Frame
	push ebp
	mov ebp, esp
	push ebx

	; Get address of lock
	mov ebx, dword ptr 8[ebp]
	mov dword ptr [ebx], 0

	; Release stack frame
	xor eax, eax
	pop ebx
	pop ebp
	ret
_spinlock_release endp
_text ends

end
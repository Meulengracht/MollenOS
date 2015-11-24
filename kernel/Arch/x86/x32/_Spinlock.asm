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
bits 32
segment .text

;Functions in this asm
global _SpinlockReset
global __spinlock_acquire
global __spinlock_release

; void SpinlockReset(Spinlock_t *Spinlock)
; We null the lock
_SpinlockReset:
	; Stack Frame
	push ebp
	mov ebp, esp

	; Save stuff
	push ebx

	; Get address of lock
	mov ebx, dword [ebp + 8]

	; Sanity
	test ebx, ebx
	je .done

	; Ok, we assume valid pointer, set it to 0
	mov dword [ebx], 0
	mov dword [ebx + 4], 0
	mov dword [ebx + 8], 0xFFFFFFFF

	; Release stack frame
	.done:
	pop ebx
	pop ebp
	ret 

; int spinlock_acquire(spinlock_t *spinlock)
; We wait for the spinlock to become free
; then set value to 1 to mark it in use.
__spinlock_acquire:
	; Stack Frame
	push ebp
	mov ebp, esp
	
	; Save stuff
	push ebx

	; Get address of lock
	mov ebx, dword [ebp + 8]

	; Sanity
	test ebx, ebx
	je .gotlock

	; We use this to test
	mov eax, 1

	; Try to get lock
	.trylock:
	xchg dword [ebx], eax
	test eax, eax
	je .gotlock

	; Busy-wait loop
	.lockloop:
	pause
	cmp dword [ebx], 0
	jne .lockloop
	jmp .trylock

	.gotlock:
	; Release stack frame
	mov eax, 1
	pop ebx
	pop ebp
	ret


; void spinlock_release(spinlock_t *spinlock)
; We set the spinlock to value 0
__spinlock_release:
	; Stack Frame
	push ebp
	mov ebp, esp

	; Save stuff
	push ebx

	; Get address of lock
	mov ebx, dword [ebp + 8]
	
	; Sanity
	test ebx, ebx
	je .done

	; Ok, we assume valid pointer, set it to 0
	mov dword [ebx], 0

	; Release stack frame
	.done:
	pop ebx
	pop ebp
	ret
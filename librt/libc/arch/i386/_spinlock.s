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
global __spinlock_acquire
global __spinlock_test
global __spinlock_release

; int spinlock_acquire(spinlock_t *spinlock)
; We wait for the spinlock to become free
; then set value to 1 to mark it in use.
__spinlock_acquire:
	push ebp
	mov ebp, esp
	
	push ebx

	; Get address of lock
	mov ebx, dword [ebp + 8]
	mov eax, 1

	.trylock:
		xchg byte [ebx], al
		test eax, eax
		je .gotlock

	.lockloop:
		pause
		cmp byte [ebx], 0
		jne .lockloop
		jmp .trylock

	.gotlock:
		mov eax, 1
		pop ebx
		pop ebp
		ret

; int spinlock_test(spinlock_t *spinlock)
; This tests whether or not spinlock is
; set or not
__spinlock_test:
	push ebp
	mov ebp, esp
	
	push ebx

	mov ebx, dword [ebp + 8]
	mov eax, 1

	; Try to get lock
	xchg byte [ebx], al
	test eax, eax
	je .gotlock

	; nah, no lock for us
	mov eax, 0
	jmp .end

	.gotlock:
		mov eax, 1

	.end:
		pop ebx
		pop ebp
		ret


; void spinlock_release(spinlock_t *spinlock)
; We set the spinlock to value 0
__spinlock_release:
	push ebx
	
	mov ebx, dword [esp + 8]
	xor eax, eax
	xchg byte [ebx], al

	pop ebx
	ret

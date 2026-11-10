/**
 * Copyright, Philip Meulengracht
 *
 * This program is free software : you can redistribute it and / or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation ? , either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 */

// User memory copy routines
// - The caller must hold UserCopyLock and must have checked every page in the
//   range before calling these. LDTR/STTR do the accesses as unprivileged, so
//   the user page permissions apply and PAN can be left as it is.
// - Arguments are x0 = destination, x1 = source and x2 = length. Only
//   caller-saved registers are used.
.text
.p2align 2
.globl ArchCopyFromUser
ArchCopyFromUser:
    cmp x2, #8
    b.lo .Lfrom_bytes

.Lfrom_words:
    ldtr x3, [x1]
    add x1, x1, #8
    str x3, [x0], #8
    sub x2, x2, #8
    cmp x2, #8
    b.hs .Lfrom_words

.Lfrom_bytes:
    cbz x2, .Lfrom_done

.Lfrom_byte:
    ldtrb w3, [x1]
    add x1, x1, #1
    strb w3, [x0], #1
    subs x2, x2, #1
    b.ne .Lfrom_byte

.Lfrom_done:
    ret

.p2align 2
.globl ArchCopyToUser
ArchCopyToUser:
    cmp x2, #8
    b.lo .Lto_bytes

.Lto_words:
    ldr x3, [x1], #8
    sttr x3, [x0]
    add x0, x0, #8
    sub x2, x2, #8
    cmp x2, #8
    b.hs .Lto_words

.Lto_bytes:
    cbz x2, .Lto_done

.Lto_byte:
    ldrb w3, [x1], #1
    sttrb w3, [x0]
    add x0, x0, #1
    subs x2, x2, #1
    b.ne .Lto_byte

.Lto_done:
    ret

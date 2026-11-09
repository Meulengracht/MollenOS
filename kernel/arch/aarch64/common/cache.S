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

// This routine must not write to the stack while it walks the caches. A store
// made after an inner level was cleaned would dirty a line in that level
// again, and the walk over the outer levels would never clean it. So it's a
// leaf routine that only uses the caller-saved registers x0-x17.
.text
.p2align 2
.globl Arm64FlushCpuCaches
Arm64FlushCpuCaches:
    mrs x0, daif
    msr daifset, #15
    mrs x1, csselr_el1
    dsb sy

    // Walk each data or unified cache level up to CLIDR.LoC. CCSIDR has a
    // legacy and a FEAT_CCIDX layout. The UEFI loader only accepts a CCIDX
    // value of 0 or 1, so nonzero here means the FEAT_CCIDX layout.
    mrs x7, id_aa64mmfr2_el1
    ubfx x7, x7, #20, #4
    mrs x9, clidr_el1
    ubfx x10, x9, #24, #3
    mov x11, #0

.Lcache_level:
    cmp x11, x10
    b.ge .Lcache_done
    add x12, x11, x11, lsl #1
    lsr x12, x9, x12
    and x12, x12, #7
    cmp x12, #2
    b.lt .Lnext_cache

    // Select the data or unified cache (InD=0) at this level in CSSELR. The
    // ISB makes sure the CCSIDR read below sees the new selection.
    lsl x12, x11, #1
    msr csselr_el1, x12
    isb
    mrs x13, ccsidr_el1
    and x14, x13, #7
    add x14, x14, #4
    cbnz x7, .Lcache_ccidx
    ubfx x15, x13, #3, #10
    ubfx x13, x13, #13, #15
    b .Lcache_geometry

.Lcache_ccidx:
    ubfx x15, x13, #3, #21
    ubfx x13, x13, #32, #24

.Lcache_geometry:
    // CCSIDR holds the number of ways and sets, each minus one. DC CISW wants
    // the way in the top bits of its 32-bit operand, and the set just above
    // the line offset bits. A direct-mapped cache gives CLZ(0) = 32, which is
    // fine, as the way is always 0 and the shift still produces zero.
    clz w16, w15

.Lcache_set:
    mov x3, x15

.Lcache_way:
    lsl x4, x3, x16
    lsl x5, x13, x14
    orr x4, x4, x5
    orr x4, x4, x12
    dc cisw, x4
    subs x3, x3, #1
    b.ge .Lcache_way
    subs x13, x13, #1
    b.ge .Lcache_set

.Lnext_cache:
    // Wait for this level to finish before we clean the next outer level.
    dsb sy
    add x11, x11, #1
    b .Lcache_level

.Lcache_done:
    msr csselr_el1, x1
    dsb sy
    ic iallu
    dsb sy
    isb
    // Put back the caller's exception mask, as the caller may have had
    // interrupts masked already.
    msr daif, x0
    ret

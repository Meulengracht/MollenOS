// Userspace-only leaf routines. LIBC_KERNEL uses memcpy_base and must neither
// clobber user SIMD state nor depend on MOPS being available/enabled.
#ifndef LIBC_KERNEL
.text

// .p2align 4 aligns the entry point to 2^4 = 16 bytes, not the input buffers.
.p2align 4
.globl asm_memcpy_neon

// void asm_memcpy_neon(void* dst, const void* src, size_t loops, size_t tail)
// AAPCS64 arguments: x0 = destination, x1 = source, x2 = count / 128,
// x3 = count % 128. All counters/pointers are 64-bit; no size_t truncation.
// q0..q7 are the 128-bit views of caller-saved v0..v7. w4 is byte scratch
// (writing w4 zero-extends into x4). x0..x4, v0..v7 and NZCV may be clobbered;
// x18 (platform/TLS), x19..x29 and callee-saved v8..v15 are untouched.
// This is a void helper: the C wrapper retains and returns the original dst.
// No calls or stack storage are needed, so x30/LR remains the return address.
// Buffers must be nonoverlapping, readable/writable Normal memory, as for
// memcpy. Unaligned accesses are supported under the userspace memory policy.
asm_memcpy_neon:
    // CBZ does not dereference either pointer when there are no full blocks.
    cbz x2, .Lneon_tail

.Lneon_loop:
    // Each LDP/STP transfers two 16-byte Q registers. Post-index #32 advances
    // the relevant pointer after the access; four pairs cover exactly 128
    // bytes. Loading the block before storing exposes independent accesses
    // to the CPU, but does not make this a general overlapping-buffer memmove.
    ldp q0, q1, [x1], #32
    ldp q2, q3, [x1], #32
    ldp q4, q5, [x1], #32
    ldp q6, q7, [x1], #32
    stp q0, q1, [x0], #32
    stp q2, q3, [x0], #32
    stp q4, q5, [x0], #32
    stp q6, q7, [x0], #32
    
    // SUBS decrements the block count and sets NZCV. B.NE repeats while Z=0.
    subs x2, x2, #1
    b.ne .Lneon_loop

.Lneon_tail:
    // Only complete 16-byte vectors are accessed. CMP sets NZCV as x3-16;
    // B.LO tests unsigned less-than (C=0), leaving 0..15 bytes for the tail.
    // This avoids reading/writing beyond the requested range at a page edge.
    cmp x3, #16
    b.lo .Lneon_bytes
    ldr q0, [x1], #16
    str q0, [x0], #16
    sub x3, x3, #16
    b .Lneon_tail

.Lneon_bytes:
    cbz x3, .Lneon_done

.Lneon_byte_loop:
    // LDRB zero-extends one source byte into w4; STRB stores its low 8 bits.
    // Post-index #1 advances each pointer without touching adjacent bytes.
    ldrb w4, [x1], #1
    strb w4, [x0], #1
    subs x3, x3, #1
    b.ne .Lneon_byte_loop

.Lneon_done:
    // RET branches to x30. No preserved registers or SP need restoration.
    ret

// Permit MOPS instruction encodings in this assembly only. This directive
// is not runtime detection and does not raise the CPU requirement for callers
// of the NEON routine; memcpy_select gates entry using kernel capabilities.
.arch_extension mops

.p2align 4
.globl asm_memcpy_mops

// void asm_memcpy_mops(void* dst, const void* src, size_t count)
// x0 = destination, x1 = source, x2 = byte count. These three distinct
// caller-saved registers are read/write operands; all other general-purpose
// registers, all SIMD registers, and SP are preserved. NZCV may be clobbered.
// As above, the C wrapper returns the original destination, not modified x0.
// FEAT_MOPS must be present and permitted by SCTLR_EL1.MSCEn[33] and, when
// applicable, HCRX_EL2.MSCEn[11]. The kernel must support MOPS restart faults.
asm_memcpy_mops:
    // Avoid even entering the architectural sequence for a zero-length copy.
    cbz x2, .Lmops_done
    // CPY = copy, F = forward-only, P/M/E = prologue/main/epilogue. memcpy
    // permits no overlap, so there is no need to select a backward direction.
    // The ! operands expose pointer/count updates as the CPU makes progress.
    // The prologue chooses the CPU's implementation-defined operand format
    // (Option A/B); main and epilogue complete that same sequence. x2 must not
    // be interpreted as an ordinary remaining-byte count between stages.
    // Keep all three instructions consecutive, with identical operands and
    // options: no software loop, intervening flag change or pointer update.
    // Interrupts/page faults may suspend the sequence. Incompatible operand
    // formats after migration cause a MOPS exception, for which __RestartMops
    // restores prologue-format operands and rewinds PC by one/two instructions.
    cpyfp [x0]!, [x1]!, x2!
    cpyfm [x0]!, [x1]!, x2!
    cpyfe [x0]!, [x1]!, x2!
.Lmops_done:
    ret
#endif

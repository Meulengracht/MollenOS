/*
 * Transfer control to the ARM64 kernel.
 *
 * x0 is the boot descriptor, x1 the physical entry, and x2 the kernel stack.
 * Arm64Prepare verifies these objects are identity mapped before this routine
 * disables the firmware translation state.
 *
 * Do not access the stack or data after cache maintenance. Clean and invalidate
 * both legacy and FEAT_CCIDX cache layouts before changing translation state.
 */
.text
.balign 4096
.globl Arm64Enter
Arm64Enter:
    // Mask asynchronous exceptions during the handoff.
    msr daifset, #15

    // Route exceptions to the local wait loop before changing exception levels.
    // Firmware vectors may become invalid during the transition.
    // CurrentEL uses 4 for EL1 and 8 for EL2.
    mrs x17, CurrentEL
    adr x16, Arm64Vectors
    cmp x17, #8
    b.eq 1f
    msr vbar_el1, x16
    b 2f
1:  msr vbar_el2, x16
2:  isb
    dsb sy

    // Clean and invalidate data/unified caches before removing firmware mappings.
    // This writes back boot data and page tables, then drops stale cache lines.
    mrs x7, id_aa64mmfr2_el1
    ubfx x7, x7, #20, #4 // Arm64CheckCpu restricts CCIDX to 0 or 1.
    mrs x9, clidr_el1
    ubfx x10, x9, #24, #3
    mov x11, #0
3:  cmp x11, x10
    b.ge 8f
    // Skip instruction-only levels reported by CLIDR.
    add x12, x11, x11, lsl #1
    lsr x12, x9, x12
    and x12, x12, #7
    cmp x12, #2
    b.lt 7f

    // Select the cache level before reading CCSIDR.
    lsl x12, x11, #1
    msr csselr_el1, x12
    isb
    mrs x13, ccsidr_el1

    // CCSIDR reports log2(line size)-4, ways-1, and sets-1 for the DC CISW operand.
    and x14, x13, #7
    add x14, x14, #4
    cbnz x7, .Lccidx
    ubfx x15, x13, #3, #10
    ubfx x13, x13, #13, #15
    b .Lcache_geometry_ready

.Lccidx:
    // CCIDX moves the ways and sets fields. The legacy fields would miss cache sets.
    ubfx x15, x13, #3, #21
    ubfx x13, x13, #32, #24

.Lcache_geometry_ready:
    clz w16, w15
4:  mov x3, x15
5:  lsl x4, x3, x16
    lsl x5, x13, x14
    orr x4, x4, x5
    orr x4, x4, x12
    dc cisw, x4
    subs x3, x3, #1
    b.ge 5b
    subs x13, x13, #1
    b.ge 4b
7:  dsb sy // Finish this cache level before moving to the next.
    add x11, x11, #1
    b 3b
8:  msr csselr_el1, xzr

    // Complete cache maintenance before changing translation state.
    dsb sy
    ic iallu
    dsb sy
    isb

    // EL1 path: disable its MMU and caches, then discard its old translations.
    // The kernel installs its own mappings.
    cmp x17, #8
    b.eq 9f
    mrs x3, sctlr_el1
    bic x3, x3, #1
    bic x3, x3, #4
    bic x3, x3, #0x1000
    msr sctlr_el1, x3
    isb
    tlbi vmalle1
    dsb sy
    isb
    msr cpacr_el1, xzr
    msr cntkctl_el1, xzr
    msr cntp_ctl_el0, xzr
    msr cntv_ctl_el0, xzr
    msr spsel, #1
    mov sp, x2

    // The kernel entry point is physical and identity mapped at this moment.
    mov x16, x1
    mov x1, xzr
    mov x2, xzr
    mov x3, xzr
    mov x18, xzr
    mov x29, xzr
    adr x30, Arm64Vectors
    br x16
9:  mrs x3, sctlr_el2
    bic x3, x3, #1
    bic x3, x3, #4
    bic x3, x3, #0x1000
    msr sctlr_el2, x3
    isb
    tlbi alle2
    dsb sy
    isb

    // EL2 path: start EL1 in AArch64 and clear firmware's stage-2 and trap settings.
    // Preserve E2H because some CPUs do not allow it to be cleared.
    // Synchronize after updating HCR before accessing EL1 state.
    mrs x8, hcr_el2
    and x8, x8, #(1 << 34)
    orr x3, x8, #0x80000000
    msr hcr_el2, x3
    isb

    // Preserve x0-x2, which hold the boot descriptor, entry, and stack.
    // Skip HCRX access unless ID_AA64ISAR2_EL1 advertises MOPS.
    mrs x3, id_aa64isar2_el1
    ubfx x3, x3, #16, #4
    cmp x3, #1
    b.ne .Lmops_el2_ready
    
    // HCRX_EL2 is undefined when ID_AA64MMFR1_EL1.HCX is zero.
    mrs x3, id_aa64mmfr1_el1
    ubfx x3, x3, #40, #4
    cbz x3, .Lmops_el2_ready
    
    // Use the encoded register name for baseline assembler targets.
    // Set HCRX_EL2.MSCEn for EL1/EL0 MOPS and preserve the other controls.
    // EL3 must allow HCRX access through SCR_EL3.HXEn.
    // The kernel enables SCTLR_EL1.MSCEn before using MOPS in userspace.
    mrs x3, S3_4_C1_C2_2
    orr x3, x3, #0x800
    msr S3_4_C1_C2_2, x3
    isb

.Lmops_el2_ready:
    // Start EL1 with SCTLR_EL1.RES1 set and M/C/I clear. Under VHE, use
    // SCTLR_EL12 to avoid changing SCTLR_EL2.
    mov x4, #0x0800
    movk x4, #0x30d0, lsl #16
    adr x16, Arm64Vectors
    cbnz x8, .Lvhe_el1_state

    mov x3, #0x33ff // Non-VHE CPTR_EL2 RES1; FP traps to EL2 disabled.
    msr cptr_el2, x3
    mov x3, #3 // Non-VHE EL1PCEN[1] and EL1PCTEN[0].
    msr cnthctl_el2, x3
    msr cpacr_el1, xzr
    msr cntkctl_el1, xzr
    msr cntp_ctl_el0, xzr
    msr cntv_ctl_el0, xzr
    msr sctlr_el1, x4
    msr vbar_el1, x16
    b .Lel1_state_ready

.Lvhe_el1_state:
    // With E2H, CPTR_EL2 uses FPEN instead of TFP. Keep EL2 FP traps disabled
    // so EL1 can enable FP/SIMD later. EL1 starts with access disabled; SVE/SME stay off.
    mov x3, #0x300000
    msr cptr_el2, x3
    mov x3, #0xc00 // VHE EL1PTEN[11] and EL1PCTEN[10].
    msr cnthctl_el2, x3

    // Use encoded EL12/EL02 names for baseline assembler targets.
    // This path runs only when firmware has enabled VHE.
    msr S3_5_C1_C0_2, xzr   // CPACR_EL12
    msr S3_5_C14_C1_0, xzr  // CNTKCTL_EL12
    msr S3_5_C14_C2_1, xzr  // CNTP_CTL_EL02
    msr S3_5_C14_C3_1, xzr  // CNTV_CTL_EL02
    msr S3_5_C1_C0_0, x4    // SCTLR_EL12
    msr S3_5_C12_C0_0, x16  // VBAR_EL12
    msr S3_4_C14_C3_1, xzr  // CNTHV_CTL_EL2 (requires VHE).

.Lel1_state_ready:
    msr cntvoff_el2, xzr
    msr cnthp_ctl_el2, xzr
    // Invalidate EL1 translations after clearing HCR.TGE, including under VHE.
    isb
    tlbi vmalls12e1
    dsb sy
    isb
    msr sp_el1, x2
    msr elr_el2, x1

    // Enter EL1h with exceptions masked and SP_EL1 set to x2. ERET loads the
    // physical entry from ELR_EL2 as it changes exception levels.
    mov x3, #0x3c5
    msr spsr_el2, x3
    mov x1, xzr
    mov x2, xzr
    mov x3, xzr
    mov x18, xzr
    mov x29, xzr
    adr x30, Arm64Vectors
    isb
    eret

/*
 * Keep this table available until the kernel installs its vectors. Each slot
 * stops on an exception instead of returning to firmware after ExitBootServices.
 */
.balign 2048
Arm64Vectors:
.rept 16
    msr daifset, #15
10: wfe
    b 10b
    .balign 128
.endr

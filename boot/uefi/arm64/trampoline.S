/*
 * Transfer control from the firmware environment to the ARM64 kernel.
 *
 * x0 contains the boot descriptor, x1 contains the physical kernel entry
 * point, and x2 contains a stack reserved for the kernel. Arm64Prepare has
 * already verified that these objects are identity mapped, which is why this
 * routine can use their physical addresses while it dismantles firmware's
 * address-space setup.
 *
 * This code deliberately avoids stack and data accesses after the final cache
 * maintenance operation. The kernel therefore receives a clean handoff even
 * though firmware's caches, translation tables, and exception vectors are no
 * longer available. The cache loop targets Cortex-A57-class systems without
 * FEAT_CCIDX, matching the platforms supported by this loader.
 */
.text
.balign 4096
.globl Arm64Enter
Arm64Enter:
    /* Mask asynchronous exceptions while the firmware state is being replaced. */
    msr daifset, #15

    /*
     * Install a private vector table before changing exception levels. Any
     * exception during the transition must stop safely rather than returning
     * into firmware through a vector table that is about to become invalid.
     * CurrentEL is encoded in the upper bits: 4 means EL1 and 8 means EL2.
     */
    mrs x17, CurrentEL
    adr x16, Arm64Vectors
    cmp x17, #8
    b.eq 1f
    msr vbar_el1, x16
    b 2f
1:  msr vbar_el2, x16
2:  isb
    dsb sy

    /*
     * Clean and invalidate every data/unified cache by set and way. The
     * firmware may have cached boot data or page-table entries using mappings
     * that the kernel will remove; cleaning first makes modified lines visible
     * in memory, while invalidating prevents stale lines surviving the switch.
     */
    mrs x9, clidr_el1
    ubfx x10, x9, #24, #3
    mov x11, #0
3:  cmp x11, x10
    b.ge 8f
    /* Extract this cache level's type from CLIDR and skip instruction-only levels. */
    add x12, x11, x11, lsl #1
    lsr x12, x9, x12
    and x12, x12, #7
    cmp x12, #2
    b.lt 7f

    /* Select the level so CCSIDR describes the cache currently being walked. */
    lsl x12, x11, #1
    msr csselr_el1, x12
    isb
    mrs x13, ccsidr_el1

    /*
     * CCSIDR reports log2(words per line) minus its architectural encoding,
     * the number of ways minus one, and the number of sets minus one. The
     * following values turn those fields into shifts and loop bounds for the
     * DC CISW operand: {way, set, level}.
     */
    and x14, x13, #7
    add x14, x14, #4
    ubfx x15, x13, #3, #10
    clz w16, w15
    ubfx x13, x13, #13, #15
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
7:  add x11, x11, #1
    b 3b
8:  msr csselr_el1, xzr

    /* Finish cache maintenance before changing translation or exception state. */
    dsb sy
    ic iallu
    dsb sy
    isb

    /*
     * EL1 firmware path: disable MMU, data caching, and instruction caching,
     * then invalidate the old EL1 translations. The kernel starts from a
     * known-off state and is responsible for installing its own mappings.
     */
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

    /* The kernel entry point is physical and identity mapped at this moment. */
    br x1
9:  mrs x3, sctlr_el2
    bic x3, x3, #1
    bic x3, x3, #4
    bic x3, x3, #0x1000
    msr sctlr_el2, x3
    isb
    tlbi alle2
    dsb sy
    isb

    /*
     * EL2 firmware path: make EL1 AArch64, disable the EL2 MMU/caches, and
     * clear EL2's translation cache before handing execution to EL1. HCR.RW
     * is the bit that selects AArch64 execution for the lower exception level.
     */
    mov x3, #0x80000000
    msr hcr_el2, x3
    
    // x3 is scratch: retain x0 (boot handoff), x1 (kernel entry), x2 (stack).
    // ID_AA64ISAR2_EL1.MOPS[19:16]=1 advertises CPY*/SET*; 0 means absent.
    // UBFX extracts four bits to the low end of x3. Skip the optional
    // hypervisor access unless the CPU actually supplies MOPS instructions.
    mrs x3, id_aa64isar2_el1
    ubfx x3, x3, #16, #4
    cmp x3, #1
    b.ne .Lmops_el2_ready
    
    // ID_AA64MMFR1_EL1.HCX[43:40] reports extended hypervisor controls:
    // zero means HCRX_EL2 does not exist, so MRS/MSR of it would be undefined.
    mrs x3, id_aa64mmfr1_el1
    ubfx x3, x3, #40, #4
    cbz x3, .Lmops_el2_ready
    
    // HCRX_EL2 = S3_4_C1_C2_2 (op0=3, op1=4, CRn=1, CRm=2, op2=2).
    // The encoded name works with baseline assembler architecture settings.
    // Set MSCEn[11]=1 (0x800) to permit memory copy/set at EL1/EL0 and
    // preserve all unrelated fields. ISB synchronizes the permission change.
    // Implemented EL3 must permit HCRX access through SCR_EL3.HXEn[38].
    // EL0 permission in SCTLR_EL1.MSCEn[33] is enabled later by the kernel;
    // merely detecting MOPS hardware is not sufficient for userspace memcpy.
    mrs x3, S3_4_C1_C2_2
    orr x3, x3, #0x800
    msr S3_4_C1_C2_2, x3
    isb
.Lmops_el2_ready:
    mov x3, #0x33ff // Non-VHE CPTR_EL2 RES1, FP traps disabled
    msr cptr_el2, x3
    mov x3, #3
    msr cnthctl_el2, x3
    msr cntvoff_el2, xzr
    msr cnthp_ctl_el2, xzr
    msr cpacr_el1, xzr
    msr cntkctl_el1, xzr
    msr cntp_ctl_el0, xzr
    msr cntv_ctl_el0, xzr

    /*
     * Establish the minimal EL1 control state expected by the kernel and give
     * it the same private stack and vector table it would have received from
     * the direct EL1 path.
     */
    mov x3, #0x0800
    movk x3, #0x30d0, lsl #16
    msr sctlr_el1, x3
    adr x16, Arm64Vectors
    msr vbar_el1, x16
    msr sp_el1, x2
    msr elr_el2, x1

    /*
     * Return from EL2 in EL1h with all asynchronous exceptions masked. EL1h
     * selects SP_EL1, so the kernel begins with the stack supplied in x2;
     * eret also loads the physical entry point from ELR_EL2 atomically with
     * the exception-level transition.
     */
    mov x3, #0x3c5
    msr spsr_el2, x3
    isb
    eret

/*
 * Reserved along with the loader image until the kernel installs its own
 * vectors. Every slot masks exceptions and waits forever so an unexpected
 * fault cannot fall back into firmware after ExitBootServices.
 */
.balign 2048
Arm64Vectors:
.rept 16
    msr daifset, #15
10: wfe
    b 10b
    .balign 128
.endr

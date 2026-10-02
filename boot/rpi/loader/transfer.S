// Native Pi primary-CPU handoff, restricted to the Cortex-A72/A76 boot profile.
//
// Inputs (AAPCS64, from RpiTransferToKernel):
//   x0 = finalized VBoot physical pointer, retained through the entire routine.
//   x1 = relocated physical PE entry; x2 = exclusive top of the reserved stack.
//   Current EL is EL1 or non-VHE EL2. Translation and data caching have stayed
//   off throughout loading. Firmware supplied a coherent wrapper and no dirty
//   cache lines aliasing the payload or the RAM allocated for the loaded PE.
//
// Flow:
//   1. Check the current regime before changing it; keep stop vectors installed.
//   2. Complete loader writes and remove the current regime's I-cache contents.
//   3. At EL2, establish a non-virtualized AArch64 EL1 environment.
//   4. Initialize common EL1 state and use ERET to enter a local EL1h landing.
//   5. Invalidate EL1 instructions, reset SP, and branch with x0 = VBoot.
//
// There are no stack accesses or C calls here. Direct EL1 entry cannot inspect
// or repair EL2 traps/stage-2 translation: the native firmware contract must
// already permit EL1 system registers, counters, timers and cache maintenance.
// This routine does not implement arbitrary hypervisor or UEFI teardown.
// It never enables interrupts, releases secondary CPUs, or enables FP/SIMD.
//
// Register definitions: Arm A-profile System Registers, DDI 0601.
// https://developer.arm.com/documentation/ddi0601/latest/
// The baseline RES1 encodings are for A72/A76, not every future Arm extension.

.section .text, "ax"
.balign 4
.global __JumpToKernel
.type __JumpToKernel, %function
__JumpToKernel:
    // DAIFSet #0xf selects D/A/I/F. It sets PSTATE bits [9:6], masking debug,
    // SError, IRQ and FIQ delivery during the transition. Synchronous faults
    // still use the reserved wrapper's stop vectors installed by entry.S.
    msr DAIFSet, #0xf

    // CurrentEL[3:2] distinguishes which SCTLR is legal to read. No EL2 access
    // may occur on the direct EL1 path. Unsupported ELs use the stackless stop.
    mrs x9, CurrentEL
    cmp x9, #4
    b.eq .Lfrom_el1
    cmp x9, #8
    b.ne _rpi_stop

    // HCR_EL2.E2H[34] changes EL1 register aliases and timer-control encodings.
    // Native Pi firmware uses non-VHE EL2. Reject VHE before interpreting those
    // registers rather than pretending a host-hypervisor state is our profile.
    mrs x10, HCR_EL2
    tbnz x10, #34, _rpi_stop
    mrs x10, SCTLR_EL2
    b .Lcheck_state
.Lfrom_el1:
    mrs x10, SCTLR_EL1
.Lcheck_state:
    // SCTLR.M[0] and C[2] must still be zero. Disabling either without knowing
    // mappings or cleaning dirty data would risk losing the image we just built.
    // EE[25] must be zero: VBoot and PE integers use little-endian accesses.
    tbnz x10, #0, _rpi_stop
    tbnz x10, #2, _rpi_stop
    tbnz x10, #25, _rpi_stop

    // The wrapper never enabled D-cache, so its stores need completion, not a
    // set/way clean of unknown firmware cache contents. DSB SY completes both
    // image and descriptor writes before invalidating instructions. IALLU acts
    // on the current regime; the EL1 landing repeats it for the target regime.
    dsb sy
    ic iallu
    dsb sy
    isb

    cmp x9, #8
    b.ne .Lsetup_el1

    // SCTLR_EL2.I[12]=0 disables EL2 instruction caching. Preserve the other
    // EL2 controls, especially implementation/RES1 bits; M/C were checked above.
    bic x10, x10, #0x1000
    msr SCTLR_EL2, x10
    isb

    // HCR_EL2.RW[31]=1 selects AArch64 EL1. All other bits are zero: VM[0]
    // disables stage-2 translation; FMO/IMO/AMO[3:5] stop routing physical
    // exceptions to EL2; TGE[27] and the instruction/register trap controls
    // (including TSC[19] for SMC) are clear. Pi 5's EL3 SMC service is retained.
    // E2H remains zero, so subsequent EL1 register names have their normal roles.
    mov x10, #0x80000000
    msr HCR_EL2, x10
    isb

    // Permit MOPS below EL2 before handing control to the kernel. x10 is
    // scratch throughout; kernel-entry/boot-argument registers stay intact.
    // ID_AA64ISAR2_EL1.MOPS[19:16]=1 means CPY*/SET* exist (0 means absent).
    // UBFX #16,#4 extracts that unsigned field into x10[3:0]; skip older
    // CPUs rather than executing an optional system-register access on them.
    mrs x10, ID_AA64ISAR2_EL1
    ubfx x10, x10, #16, #4
    cmp x10, #1
    b.ne .Lmops_el2_ready
    
    // ID_AA64MMFR1_EL1.HCX[43:40]=0 means HCRX_EL2 is absent. A nonzero
    // field provides the extended hypervisor controls; CBZ skips if absent.
    // This check is independent of MOPS because HCRX itself is optional.
    mrs x10, ID_AA64MMFR1_EL1
    ubfx x10, x10, #40, #4
    cbz x10, .Lmops_el2_ready
    
    // S3_4_C1_C2_2 is HCRX_EL2's architectural system-register encoding:
    // op0=3, op1=4, CRn=1, CRm=2, op2=2. The encoded name also assembles
    // with a baseline architecture without enabling optional mnemonics.
    // MSCEn[11] (0x800)=1 permits memory copy/set execution at EL1/EL0;
    // zero would leave those instructions undefined despite ID support.
    // Read-modify-write preserves every other hypervisor control. ISB
    // synchronizes the change before executing at the lower exception level.
    // Firmware at EL3, if present, must allow access via SCR_EL3.HXEn[38].
    // This does not enable EL0 by itself: the kernel sets SCTLR_EL1.MSCEn[33].
    mrs x10, S3_4_C1_C2_2
    orr x10, x10, #0x800
    msr S3_4_C1_C2_2, x10
    isb
.Lmops_el2_ready:

    // VTTBR_EL2=0 removes the inherited stage-2 table/VMID association while
    // stage 2 is disabled. We do not manufacture a usable translation table.
    msr VTTBR_EL2, xzr

    // Non-VHE CPTR_EL2 needs RES1 bits [13:12] and [9:0] on the A72 baseline.
    // TFP[10], TTA[20] and TCPAC[31] stay clear: no EL2 trap for FP/SIMD, trace
    // or CPACR access. EL1's CPACR below still denies FP/SIMD to early code.
    mov x10, #0x33ff
    msr CPTR_EL2, x10

    // MDCR_EL2=0 clears EL2 debug/PMU access traps and debug exception routing.
    // HPMN[4:0]=0 does not configure a usable EL1 PMU; PMU setup is kernel work.
    msr MDCR_EL2, xzr

    // With E2H=0, CNTHCTL_EL2.EL1PCTEN[0] and EL1PCEN[1] allow the physical
    // counter and timer at EL1. CNTVOFF_EL2=0 makes virtual time share the same
    // origin. Preserve firmware's CNTFRQ_EL0; it is not the CPU clock frequency.
    mov x10, #3
    msr CNTHCTL_EL2, x10
    msr CNTVOFF_EL2, xzr

    // CNTHP_CTL_EL2.ENABLE[0]=0 stops the EL2 physical timer. Clearing its mask
    // bit [1] is harmless with ENABLE=0; ISTATUS[2] is read-only. This does not
    // configure or acknowledge any GIC interrupt source.
    msr CNTHP_CTL_EL2, xzr

.Lsetup_el1:
    // SCTLR_EL1=0x30d00800 retains the baseline RES1 bits [29,28,23,22,20,11].
    // On A76, bit 23 is SPAN: keeping it one avoids forcing PAN on exceptions.
    // M[0], C[2], I[12] are zero; EE[25]/E0E[24] select little-endian data.
    // A[1], SA[3] and SA0[4] are zero; SP is nevertheless 16-byte aligned by
    // construction. Other optional execution controls stay disabled. This is
    // the supported Pi baseline, not a feature-generic reset of arbitrary CPUs.
    mov x10, #0x0800
    movk x10, #0x30d0, lsl #16
    msr SCTLR_EL1, x10
    isb

    // TTBR0/1 and TCR are deliberately unusable placeholders while M=0. The
    // kernel must construct tables and memory attributes before enabling M.
    // Invalidate inherited EL1 translations so they cannot survive that setup.
    msr TTBR0_EL1, xzr
    msr TTBR1_EL1, xzr
    msr TCR_EL1, xzr
    isb
    tlbi vmalle1
    dsb sy
    isb

    // VBAR_EL1 names the same 2 KiB-aligned, reserved table used by the wrapper.
    // It contains exceptions until the kernel installs its own vectors; it is
    // not an interrupt implementation and must not be reclaimed in the interim.
    adr x10, _early_vector_table
    msr VBAR_EL1, x10

    // CPACR_EL1.FPEN[21:20]=0 traps FP/SIMD at EL1. Early kernel code must use
    // general registers only until it establishes a context-switch policy.
    // MDSCR_EL1=0 clears SS[0], KDE[13], MDE[15], disabling inherited stepping
    // and monitor debug. The exception masks alone do not initialize debug state.
    msr CPACR_EL1, xzr
    msr MDSCR_EL1, xzr

    // CNTKCTL_EL1=0 denies EL0 counter/timer access. CNTP_CTL_EL0 and
    // CNTV_CTL_EL0 have ENABLE[0]=0, leaving both EL1 timers stopped. Reading
    // the counters at EL1 remains possible under the firmware/EL2 policy above.
    msr CNTKCTL_EL1, xzr
    msr CNTP_CTL_EL0, xzr
    msr CNTV_CTL_EL0, xzr

    // No CPU-local or user TLS object exists yet. Clear the software-owned
    // TPIDR registers and inactive SP_EL0 rather than exposing firmware values.
    msr TPIDR_EL1, xzr
    msr TPIDR_EL0, xzr
    msr TPIDRRO_EL0, xzr
    msr SP_EL0, xzr

    // SPSR=0x3c5: D/A/I/F[9:6]=1111 and M[3:0]=0101 (EL1h, SP_EL1).
    // nRW[4]=0 selects AArch64; other saved PSTATE fields are zero. Using ERET
    // on both paths also removes inherited single-step/condition state. It is
    // legal to return from EL1 to EL1h; EL2 uses the same target state to drop EL.
    adr x10, .Lel1_landing
    mov x11, #0x3c5
    cmp x9, #8
    b.ne .Lreturn_el1
    msr SP_EL1, x2
    msr ELR_EL2, x10
    msr SPSR_EL2, x11
    isb
    eret
.Lreturn_el1:
    msr ELR_EL1, x10
    msr SPSR_EL1, x11
    isb
    eret

.Lel1_landing:
    // EL2 invalidation alone does not promise clean EL1 instruction state.
    // We land in firmware-coherent wrapper code, invalidate here at EL1, and
    // complete maintenance before fetching any newly copied PE instructions.
    ic iallu
    dsb sy
    isb

    // ERET selected SP_EL1. Only now abandon all loader frames, including the
    // direct EL1 path's active stack. No load/store follows the reset here.
    mov sp, x2
    mov x16, x1
    mov x1, xzr
    mov x2, xzr
    mov x3, xzr
    mov x18, xzr
    mov x29, xzr

    // The kernel is entered with x0=VBoot, x1-x3=0, x18=0 and no caller frame.
    // A stray RET lands in the stackless stop loop, never in abandoned C code.
    // Other general registers are scratch, not part of the published contract.
    adr x30, _rpi_stop
    br x16
.size __JumpToKernel, . - __JumpToKernel

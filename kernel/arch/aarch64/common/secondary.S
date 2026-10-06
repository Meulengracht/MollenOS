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

#include "abi.h"

// Both spin-table and PSCI secondaries enter with translation disabled.
// Match the complete MPIDR affinity; firmware arguments are not a logical ID.
.text
.p2align 6
.globl Arm64SecondaryEntry

// Firmware's x0 argument is not trusted as a core ID. x0 becomes the masked
// MPIDR affinity, x1 is the affinity mask, x2 is the search count, and x3 is
// scratch. x19 retains the selected Arm64SecondaryState across setup stages.
// There is no usable stack until the selected state's Stack field is loaded.
Arm64SecondaryEntry:
    // DAIFSet immediate 0b1111 masks Debug, SError, IRQ and FIQ (DAIF[9:6])
    // while vectors, stack and per-core execution controls are not yet ready.
    msr daifset, #ARM64_DAIF_ALL
    mrs x0, mpidr_el1
    
    // MPIDR Aff0[7:0], Aff1[15:8], Aff2[23:16], Aff3[39:32] identify the
    // physical core. Mask 0xff00ffffff removes flags such as MT[24] and U[30]
    // and reserved bits, so comparison uses the full affinity, not just Aff0.
    mov x1, #ARM64_AFFINITY_LOW
    movk x1, #ARM64_AFFINITY_HIGH, lsl #ARM64_AFFINITY_HIGH_SHIFT
    and x0, x0, x1
    adrp x19, g_arm64SecondaryState
    add x19, x19, :lo12:g_arm64SecondaryState
    
    // ADRP + :lo12: forms the state-array address without an absolute pointer.
    // There are ARM64_CPU_COUNT=256 slots, each exactly 64 bytes:
    // +0 Affinity, +8 Stack, +16 Table0, +24 Table1, +32 Tcr, +40 Mair,
    // +48 logical Id, +56 Ready. These offsets match Arm64SecondaryState in
    // smp.c and must remain synchronized with that structure's layout.
    mov x2, #ARM64_SECONDARY_SLOTS

.Lfind_secondary:
    // Ignore unpublished slots (Ready=0); compare only complete descriptors.
    ldr x3, [x19, #ARM64_SECONDARY_READY]
    cbz x3, .Lnext_secondary
    ldr x3, [x19]
    cmp x0, x3
    b.eq .Lsecondary_found

.Lnext_secondary:
    add x19, x19, #ARM64_SECONDARY_STATE_SIZE
    subs x2, x2, #1
    b.ne .Lfind_secondary
    b .Lsecondary_stop

.Lsecondary_found:
    // CurrentEL[3:2] encodes the exception level: EL2=8, EL1=4. Only touch
    // EL2-only controls on the EL2 path; halt on unsupported entry levels.
    mrs x0, CurrentEL
    cmp x0, #ARM64_CURRENT_EL2
    b.eq .Lsecondary_el2
    cmp x0, #ARM64_CURRENT_EL1
    b.ne .Lsecondary_stop
    b .Lsecondary_el1

.Lsecondary_el2:
    // HCR_EL2.RW[31]=1 selects AArch64 EL1. All other fields are zero,
    // disabling stage-2 translation, VHE aliasing and EL2 trap/routing policy.
    mov x0, #ARM64_HCR_AARCH64
    msr hcr_el2, x0
    
    // ID_AA64ISAR2_EL1.MOPS[19:16]=1 supplies CPY*/SET*, 0 means absent.
    // x0 is disposable setup scratch; UBFX puts the four-bit field in [3:0].
    mrs x0, id_aa64isar2_el1
    ubfx x0, x0, #ARM64_ISAR2_MOPS_SHIFT, #ARM64_FEATURE_FIELD_BITS
    cmp x0, #ARM64_ISAR2_MOPS_SUPPORTED
    b.ne .Lmops_el2_ready
    
    // ID_AA64MMFR1_EL1.HCX[43:40]=0 means HCRX_EL2 is not implemented.
    // Guard its access separately so older CPUs never execute that MRS/MSR.
    mrs x0, id_aa64mmfr1_el1
    ubfx x0, x0, #ARM64_MMFR1_HCX_SHIFT, #ARM64_FEATURE_FIELD_BITS
    cbz x0, .Lmops_el2_ready
    
    // S3_4_C1_C2_2 names HCRX_EL2 (op0=3, op1=4, CRn=1, CRm=2, op2=2)
    // without requiring the assembler to target a newer instruction set.
    // MSCEn[11]=1 (0x800) permits MOPS at EL1/EL0; retain other controls and
    // ISB-synchronize before ERET. EL3 must permit HCRX via SCR_EL3.HXEn[38].
    // Each secondary needs this independently of the boot core. Its EL1
    // MSCEn[33] is set later in Arm64InitializeLocal before feature publication.
    mrs x0, S3_4_C1_C2_2
    orr x0, x0, #ARM64_HCRX_MOPS_ENABLE
    msr S3_4_C1_C2_2, x0
    isb

.Lmops_el2_ready:
    // CPTR_EL2.TFP[10]/TCPAC[31]=0 removes EL2 FP/SIMD/CPACR access traps.
    // HSTR_EL2=0 clears AArch32 system-register traps; execution is AArch64.
    msr cptr_el2, xzr
    msr hstr_el2, xzr
    
    // With HCR.E2H=0, CNTHCTL_EL2.EL1PCTEN[0]/EL1PCEN[1]=1 allow EL1's
    // physical counter/timer. CNTVOFF=0 gives the virtual counter no offset;
    // VTTBR=0 removes stale stage-2 table/VMID association while HCR.VM=0.
    mov x0, #ARM64_CNTHCTL_PHYSICAL
    msr cnthctl_el2, x0
    msr cntvoff_el2, xzr
    msr vttbr_el2, xzr
    
    // SCTLR_EL1=0x30d00800 supplies the baseline RES1/control bits
    // [29,28,23,22,20,11], with MMU M[0], data cache C[2] and instruction
    // cache I[12] still disabled until this core installs the shared tables.
    mov x0, #ARM64_SCTLR_BASE_LOW
    movk x0, #ARM64_SCTLR_BASE_HIGH, lsl #16
    msr sctlr_el1, x0
    
    // SPSR_EL2=0x3c5: DAIF[9:6]=0b1111 masks asynchronous exceptions;
    // M[3:0]=0b0101 selects AArch64 EL1h using SP_EL1. ELR_EL2 receives the
    // EL1 landing address; ERET restores that PC and the prepared PSTATE.
    mov x0, #ARM64_SPSR_MASKED_EL1H
    msr spsr_el2, x0
    adr x0, .Lsecondary_el1
    msr elr_el2, x0
    eret

.Lsecondary_el1:
    // SPSel=1 selects SP_EL1. Stack (+8) is a prepared per-core stack top.
    msr spsel, #1
    ldr x0, [x19, #ARM64_SECONDARY_STACK]
    mov sp, x0
    adrp x0, Arm64Vectors
    add x0, x0, :lo12:Arm64Vectors
    msr vbar_el1, x0
    
    // VBAR_EL1 installs the EL1 exception-vector base before enabling memory.
    // CPACR_EL1.FPEN[21:20]=0b11 (0x300000) permits FP/NEON at EL0 and EL1;
    // the kernel remains general-register-only and saves userspace SIMD state.
    mov x0, #ARM64_CPACR_FP_ENABLE
    msr cpacr_el1, x0
    
    // Disable both EL0 timer controls (ENABLE[0]=0). CNTKCTL=0 also denies
    // EL0 counter/timer access until the kernel intentionally grants it.
    msr cntp_ctl_el0, xzr
    msr cntv_ctl_el0, xzr
    msr cntkctl_el1, xzr
    
    // Table0/Table1 are TTBR0_EL1/TTBR1_EL1 roots; TCR_EL1 supplies address
    // sizes, granules and walk policy; MAIR_EL1 maps attribute indices to
    // Normal/Device memory types. Use the boot core's prepared configuration.
    ldp x0, x1, [x19, #ARM64_SECONDARY_TABLES]
    msr ttbr0_el1, x0
    msr ttbr1_el1, x1
    ldp x0, x1, [x19, #ARM64_SECONDARY_CONTROLS]
    msr tcr_el1, x0
    msr mair_el1, x1
    
    // ISB applies the new translation controls. TLBI VMALLE1 discards this
    // core's EL1 stage-1 cached translations; IC IALLU invalidates its local
    // instruction cache. DSB SY completes maintenance and ISB refetches under
    // the new execution context before translation/caches are enabled.
    isb
    tlbi vmalle1
    ic iallu
    dsb sy
    isb
    
    // Preserve SCTLR's existing controls while enabling M[0], C[2], I[12].
    // The final ISB makes the new regime effective before entering C code.
    mrs x0, sctlr_el1
    orr x0, x0, #ARM64_SCTLR_MMU
    orr x0, x0, #ARM64_SCTLR_DATA_CACHE
    orr x0, x0, #ARM64_SCTLR_INSTRUCTION_CACHE
    msr sctlr_el1, x0
    isb
    
    // x0 now becomes Arm64SecondaryStart's logical-ID argument (+48).
    // x18 is the platform/TLS register; clear it until TLS is initialized.
    // x29=0 terminates the frame chain. BL sets x30/LR; the C entry never
    // returns normally, and the stop loop is a fail-safe with no stack use.
    ldr x0, [x19, #ARM64_SECONDARY_ID]
    mov x18, xzr
    mov x29, xzr
    bl Arm64SecondaryStart

.Lsecondary_stop:
    wfe
    b .Lsecondary_stop

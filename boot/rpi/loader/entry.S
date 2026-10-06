// Native Raspberry Pi bootstrap: establish just enough state to run loader C.
//
// Entry contract:
//   x0 = firmware DTB physical address; x1 = board selector from boot_4/5.S.
//   Execution is AArch64 at EL1 or EL2, with MMU and data cache disabled.
//   The wrapper resides at its linked physical address; no stack is trusted.
//   This is a native firmware entry, not a general hypervisor/chainloader ABI:
//   little-endian data accesses and usable privileged instructions are assumed.
//   Checking SCTLR below does not validate EL2 trap policy or stage-2 mappings.
//
// Flow and ownership:
//   1. Preserve firmware arguments and park CPUs with nonzero affinity before
//      they can share a stack or race the primary CPU's BSS initialization.
//   2. Select the current EL's control registers, mask asynchronous exceptions,
//      and install vectors that can stop without using memory or a stack.
//   3. Reject translation/cache state that this bootstrap cannot safely undo.
//   4. Select a private current-EL stack, then clear only the wrapper's BSS.
//   5. Enter RpiLoaderInit with the saved arguments. It does not return.
//
// No EL transition happens here. transfer.S establishes the EL1 contract after
// discovery, PE loading, and VBoot construction.
// Early rejection/exception paths converge on _rpi_stop; they cannot assume
// initialized C globals or a UART and therefore do not publish a C status code.
//
// Register roles until the C call:
//   x19 = saved DTB, x21 = saved board selector; neither is scratch storage.
//   x9-x11 = scratch; x10 carries SCTLR into the shared state checks.
//   x20 = debugger marker, set to 1 when those state checks are reached.
// Architectural register definitions: Arm A-profile System Registers (DDI 0601),
// https://developer.arm.com/documentation/ddi0601/latest/

#include "registers.h"

.section .text, "ax"

.global _rpi_initialize

_rpi_initialize:
    // Later register checks need scratch registers without losing the firmware
    // inputs. No stack save is possible before we establish our own storage.
    mov x19, x0
    mov x21, x1

    // MPIDR_EL1 identifies a CPU by four affinity fields, not a linear CPU ID:
    // Aff0=[7:0], Aff1=[15:8], Aff2=[23:16], Aff3=[39:32]. Read-only here.
    // Extract only affinity; unrelated flags (including MT[24] and U[30]) must
    // not affect primary selection. OR is sufficient because we only ask
    // whether every affinity field is zero, not reconstruct a unique CPU ID.
    // Pi 5 can distinguish CPUs above Aff0, so checking only low bits would
    // allow multiple CPUs to clear BSS and run on this one bootstrap stack.
    // Zero affinity is our primary-CPU policy, not an architectural guarantee.
    mrs x9, MPIDR_EL1
    and x10, x9, #RPI_MPIDR_AFF012_MASK
    ubfx x9, x9, #RPI_MPIDR_AFF3_SHIFT, #RPI_MPIDR_AFF3_WIDTH
    orr x10, x10, x9
    cbnz x10, _rpi_stop

    // CurrentEL encodes the exception level in bits [3:2]: EL1=0x4, EL2=0x8.
    // Read before accessing EL-specific registers: EL1 cannot configure EL2.
    // EL3 is outside this native boot contract and is parked. EL0 is not a
    // supported entry either; privileged reads can trap before reaching here.
    mrs x9, CurrentEL
    cmp x9, #RPI_CURRENT_EL1
    b.eq .Lel1
    cmp x9, #RPI_CURRENT_EL2
    b.ne _rpi_stop

    // DAIFSet's immediate is a selector mask, not the DAIF register bit layout:
    // immediate [3:0] selects D,A,I,F respectively. #0xf sets all four masks:
    // PSTATE.D[9] masks debug exceptions where architecturally applicable,
    // PSTATE.A[8] masks SError, I[7] masks IRQ, and F[6] masks FIQ.
    // A set mask inhibits delivery; this does not disable interrupt sources
    // in the GIC, prevent synchronous faults, or override higher-EL routing.
    // Mask before replacing vectors: no normal handler or valid stack exists.
    msr DAIFSet, #RPI_DAIF_ALL

    // SCTLR_EL2 controls the current EL2 environment. Snapshot it for the
    // common M/C checks below; do not write it or reset implementation-defined
    // and RES1 fields by manufacturing a new control-register value.
    mrs x10, SCTLR_EL2

    // VBAR_EL2 supplies the base of exceptions taken to EL2. ADR obtains the
    // table's nearby address without a memory load. Its 2 KiB alignment leaves
    // bits [10:0] zero, as required for the architectural vector-slot offsets.
    // This replaces only EL2 vectors; it does not initialize EL1's VBAR.
    adr x11, _early_vector_table
    msr VBAR_EL2, x11
    b .Lcheck_state

.Lel1:
    // Same DAIFSet selector bits as above: mask D/A/I/F while there is no
    // exception-handling infrastructure. Synchronous exceptions still need
    // the stop vectors, and exceptions routed to EL2 are outside our control.
    msr DAIFSet, #RPI_DAIF_ALL

    // At EL1, SCTLR_EL1 is the relevant stage-1 control register. Reading
    // SCTLR_EL2 instead would require privileges that this path does not have.
    mrs x10, SCTLR_EL1

    // Install the same aligned stop table for exceptions taken to EL1. VBAR
    // selects a table address; it neither unmasks exceptions nor changes EL.
    adr x11, _early_vector_table
    msr VBAR_EL1, x11

.Lcheck_state:
    // x20 is only a debugger breadcrumb, not a PSTATE or control-register bit.
    mov x20, #1

    // SCTLR_ELx.M[0]=1 means stage-1 address translation is enabled. Our linker
    // symbols name physical storage, so using an unknown mapping is unsafe.
    // Merely clearing M could strand the executing PC or firmware-owned data;
    // without a mapping/ownership proof, stop instead of attempting recovery.
    tbnz x10, #RPI_SCTLR_M_BIT, _rpi_stop

    // SCTLR_ELx.C[2]=1 enables data/unified caching under its regime. We require
    // C=0 at entry: blindly disabling or invalidating caches can discard dirty
    // DTB/payload data. This test neither cleans caches nor proves that an
    // arbitrary predecessor left no dirty lines; native firmware is assumed.
    // SCTLR_ELx.I[12] (instruction-cache enable) is deliberately left alone.
    // We have not copied executable code; later PE loading must synchronize
    // its writes with instruction fetch before entering the loaded image.
    tbnz x10, #RPI_SCTLR_C_BIT, _rpi_stop

    // Context synchronization makes the preceding VBAR update visible to the
    // following execution. ISB is not a data-cache clean or a memory-write
    // completion barrier, and cannot substitute for the later PE cache work.
    isb

    // SPSel immediate bit 0 sets PSTATE.SP: 1 selects SP_ELn for the current
    // EL (SP_EL1 or SP_EL2); 0 would select the shared SP_EL0 instead.
    // Select first, then write SP so we initialize the intended bank. This
    // changes neither exception level nor the dormant stack at another EL.
    msr SPSel, #1
    
    // The stack grows downward from its exclusive upper bound. The linker
    // guarantees 16-byte alignment for AAPCS64 C calls. No firmware stack is
    // reused, and no push/call occurs before this assignment.
    ldr x9, =__rpi_boot_stack_top
    mov sp, x9
    
    // A zero frame pointer marks the end of the bootstrap frame chain rather
    // than exposing an inherited firmware frame as a valid loader caller.
    mov x29, xzr

    // C static storage must begin at zero even when RAM retains old contents.
    // Clear [__bss_start, __bss_end), whose linker-defined bounds exclude the
    // active stack, payload, and DTB. Byte stores tolerate any BSS tail size
    // and avoid alignment assumptions before normal memory setup exists.
    // This is wrapper BSS only; the PE loader will zero the kernel's storage.
    ldr x9, =__bss_start
    ldr x10, =__bss_end
.Lclear_bss:
    cmp x9, x10
    b.hs .Lready
    strb wzr, [x9], #1
    b .Lclear_bss
.Lready:
    // AAPCS64 arguments: x0=DTB physical address, w1=board selector. The C
    // loader now has zeroed globals and a private aligned stack, but still
    // runs at the incoming EL with D/A/I/F masked and no MMU setup of its own.
    // BL supplies a return address for ordinary C entry; falling through to
    // the stop loop also contains an unexpected return from this no-return API.
    mov x0, x19
    mov x1, x21
    bl RpiLoaderInit

.global _rpi_stop
_rpi_stop:
    // WFE is a wait hint, not a permanent halt: an event (or implementation
    // behavior) may resume execution. Always loop; waking is not permission
    // to join initialization. No shared memory, stack, or console is needed.
    // Secondary release must eventually use the platform's startup protocol.
    wfe
    b _rpi_stop

// AArch64 vectors occupy 16 slots of 128 bytes at a 2 KiB-aligned base.
// Four groups select current EL with SP_EL0, current EL with SP_ELn, lower EL
// in AArch64, or lower EL in AArch32. Each group has synchronous, IRQ, FIQ,
// and SError slots. Every slot stops because no recovery context exists yet.
// A direct branch uses no stack and does not overwrite the exception's ELR or
// ESR diagnostic registers. The remaining 124 bytes preserve slot spacing;
// they are padding, not executable handlers. Masked interrupts do not make
// this table optional: synchronous faults can still occur during bootstrap.
.balign RPI_VECTOR_ALIGNMENT
.global _early_vector_table
_early_vector_table:
.rept RPI_VECTOR_COUNT
    b _rpi_stop
    .space RPI_VECTOR_SLOT_PADDING
.endr

// File-backed storage makes the stack part of the image length visible to
// firmware, so followkernel placement cannot reuse it as apparently free RAM.
// The 64 KiB budget is bootstrap-only; page alignment permits future guarded
// mappings, but there is no guard page while the MMU is disabled. The linker
// places this outside the BSS clearing range so initialization cannot erase
// an active frame. Initial zero bytes reserve storage; C does not require a
// zero-filled stack, only valid writable space and ABI alignment.
.section .boot.stack, "aw", %progbits
.balign RPI_BOOT_STACK_ALIGNMENT

.global __rpi_boot_stack_bottom
__rpi_boot_stack_bottom:
    .space RPI_BOOT_STACK_SIZE, 0

.global __rpi_boot_stack_top
__rpi_boot_stack_top:

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

#ifndef __VALI_AARCH64_PRIVATE_H__
#define __VALI_AARCH64_PRIVATE_H__

#include <arch/aarch64/arch.h>
#include <devicetree.h>
#include <vboot/vboot.h>
#include "abi.h"

// GICv2 distributor byte offsets: control, capabilities, groups, enable/disable,
// pending clear, priorities, CPU targets, trigger configuration, and SGI request.
#define ARM64_GICD_CTLR         0x000
#define ARM64_GICD_TYPER        0x004
#define ARM64_GICD_IGROUPR      0x080
#define ARM64_GICD_ISENABLER    0x100
#define ARM64_GICD_ICENABLER    0x180
#define ARM64_GICD_ICPENDR      0x280
#define ARM64_GICD_IPRIORITYR   0x400
#define ARM64_GICD_ITARGETSR    0x800
#define ARM64_GICD_ICFGR        0xc00
#define ARM64_GICD_SGIR         0xf00

// GICv2 CPU-interface byte offsets: control, priority threshold, priority split,
// interrupt acknowledge, and end of interrupt. IAR/EOIR carry the full SGI token.
#define ARM64_GICC_CTLR         0x00
#define ARM64_GICC_PMR          0x04
#define ARM64_GICC_BPR          0x08
#define ARM64_GICC_IAR          0x0c
#define ARM64_GICC_EOIR         0x10

// GIC register packing: one enable bit, one priority/target byte, or two
// configuration bits per interrupt, in a 32-bit MMIO word.
#define ARM64_GIC_REGISTER_SIZE       4U
#define ARM64_GIC_BITS_PER_REGISTER   32U
#define ARM64_GIC_BYTES_PER_REGISTER  4U
#define ARM64_GIC_CONFIG_PER_REGISTER 16U
#define ARM64_GIC_CONFIG_BITS         2U
#define ARM64_GIC_CONFIG_MASK         3U
#define ARM64_GIC_CONFIG_EDGE         2U

// TYPER describes groups of 32 lines; bit 10 reports security extensions.
#define ARM64_GIC_TYPER_LINES_MASK    0x1fU
#define ARM64_GIC_TYPER_SECURITY      (1U << 10)

// Control bit zero enables the distributor or the visible CPU-interface group.
#define ARM64_GIC_ENABLE              1U

// Banked interrupt ranges: SGIs 0-15, PPIs 16-31, then shared peripheral IRQs.
#define ARM64_GIC_PPI_BASE            16U
#define ARM64_GIC_SPI_BASE            32U
#define ARM64_GIC_PPI_MASK            0xffff0000U
#define ARM64_GIC_SGI_MASK            0x0000ffffU
#define ARM64_GIC_ALL_BITS            0xffffffffU

// Priorities and CPU target lists occupy one byte; repeat a byte across a word.
#define ARM64_GIC_BYTE_MASK           0xffU
#define ARM64_GIC_REPEAT_BYTE         0x01010101U
#define ARM64_GIC_DEFAULT_PRIORITIES  0xa0a0a0a0U

// IAR bits 9:0 are the ID; IDs 1020-1023 do not represent a usable interrupt.
#define ARM64_GIC_INTERRUPT_ID_MASK   0x3ffU
#define ARM64_GIC_SPECIAL_ID_BASE     1020U

// SGIR bits 23:16 select target interfaces; Vali interrupt IDs use the low word.
#define ARM64_GIC_SGI_TARGET_SHIFT    16U
#define ARM64_INTERRUPT_VECTOR_MASK   0xffffU

// SGIs zero and one are reserved for syscall and scheduler use, respectively.
#define ARM64_GIC_ALLOCATABLE_SGI_BASE  2U
#define ARM64_GIC_ALLOCATABLE_SGI_COUNT 14U

// DAIF.I masks IRQ delivery; DAIFSet/DAIFClr encode that bit as immediate 2.
#define ARM64_DAIF_IRQ_MASK           (1U << 7)

// Four-level, 4 KiB translation tables have 512 entries and consume nine
// address bits per level, starting at bit 39 in the level-zero table.
#define ARM64_TABLE_ENTRIES      512U
#define ARM64_TABLE_INDEX_MASK   (ARM64_TABLE_ENTRIES - 1U)
#define ARM64_TABLE_LEVEL_BITS   9U
#define ARM64_TABLE_ROOT_SHIFT   39U
#define ARM64_TABLE_PAGE_LEVEL   3U
#define ARM64_TABLE_BLOCK_LEVEL  2U
#define ARM64_BLOCK_SIZE         0x200000ULL
#define ARM64_BOOT_TABLE_COUNT   256U

// Descriptor bits [47:12] hold the physical address. Bits [1:0] distinguish
// invalid, block (01), and next-table/final-page (11) entries.
#define TABLE_ADDRESS           0x0000fffffffff000ULL
#define TABLE_VALID             1ULL
#define TABLE_NEXT              3ULL
#define TABLE_TYPE_MASK         3ULL
#define TABLE_PAGE_BIT          2ULL

// AF marks an accessed page; SH=11 selects inner-shareable memory. AP bit 6
// permits EL0 access and AP bit 7 makes the page read-only.
#define PAGE_ACCESS             (1ULL << 10)
#define PAGE_INNER_SHAREABLE    (3ULL << 8)
#define PAGE_USER               (1ULL << 6)
#define PAGE_READONLY           (1ULL << 7)

// AttrIndx[4:2] selects MAIR slot 0 (normal WBWA), 1 (Device-nGnRnE), or
// 2 (normal non-cacheable RAM). Keep these indexes paired with ARM64_MAIR.
#define PAGE_DEVICE             (1ULL << 2)
#define PAGE_NORMAL_NOCACHE     (2ULL << 2)
#define PAGE_MEMORY_TYPE        (7ULL << 2)

// PXN/UXN prohibit privileged/user instruction fetches, respectively.
#define PAGE_PXN                (1ULL << 53)
#define PAGE_UXN                (1ULL << 54)

// Software-owned descriptor bits retain reservation, lifetime, trap, and
// inherited-table ownership information; they do not enable hardware access.
#define PAGE_RESERVED           (1ULL << 55)
#define PAGE_PERSISTENT         (1ULL << 56)
#define PAGE_TRAP               (1ULL << 57)
#define TABLE_INHERITED         (1ULL << 58)

// ID_AA64MMFR0_EL1: TGran4[31:28]=0xf rejects 4 KiB pages; PARange[2:0]
// is copied to TCR_EL1.IPS[34:32] to select the implemented physical width.
#define ARM64_MMFR0_GRANULE_SHIFT 28U
#define ARM64_MMFR0_NO_GRANULE    0xfU
#define ARM64_MMFR0_PA_MASK       7ULL
#define ARM64_TCR_PA_SHIFT        32U

// T0SZ/T1SZ=16 provide 48-bit virtual addresses. Both table walks use
// inner-shareable WBWA memory; TG0=00 and TG1=10 select 4 KiB granules.
#define ARM64_TCR_VA_SIZE        (16ULL | (16ULL << 16))
#define ARM64_TCR_SHAREABLE      ((3ULL << 12) | (3ULL << 28))
#define ARM64_TCR_WALK_CACHE     ((1ULL << 8) | (1ULL << 10) | (1ULL << 24) | (1ULL << 26))
#define ARM64_TCR_GRANULE        (2ULL << 30)

// MAIR slots 0/1/2: normal WBWA (0xff), Device-nGnRnE (0x00), normal NC (0x44).
#define ARM64_MAIR               0x4400ffULL

// CTR_EL0 line-size fields encode log2(words); a word contains four bytes.
#define ARM64_CTR_DATA_SHIFT     16U
#define ARM64_FEATURE_FIELD_MASK 0xfU
#define ARM64_CACHE_WORD_SIZE    4UL

// TTBR0 uses the low 48-bit VA range; bit 63 selects the upper TTBR1 range.
#define ARM64_VIRTUAL_ADDRESS_BITS 48U
#define ARM64_ADDRESS_HIGH_BIT     63U

// Physical allocation limits for legacy 24-bit, low 31-bit, and 32-bit DMA.
#define ARM64_MEMORY_MASK_LEGACY 0xffffffULL
#define ARM64_MEMORY_MASK_LOW    0x7fffffffULL
#define ARM64_MEMORY_MASK_32     0xffffffffULL

// ESR_EL1.EC[31:26] selects the exception class; ISS[24:0] carries details.
#define ARM64_ESR_SYNDROME_MASK           0x1ffffffU
#define ARM64_EC_UNKNOWN                  0x00U
#define ARM64_EC_FP_ACCESS                0x07U
#define ARM64_EC_INSTRUCTION_ABORT_USER   0x20U
#define ARM64_EC_INSTRUCTION_ABORT_KERNEL 0x21U
#define ARM64_EC_DATA_ABORT_USER          0x24U
#define ARM64_EC_DATA_ABORT_KERNEL        0x25U
#define ARM64_EC_MOPS                     0x27U
#define ARM64_EC_FP_EXCEPTION             0x2cU
#define ARM64_EC_BREAKPOINT               0x30U
#define ARM64_EC_SOFTWARE_STEP            0x32U
#define ARM64_EC_BRK                      0x3cU

// Abort FSC[5:2]=0001 identifies translation faults at any table level;
// data-abort WnR[6] distinguishes writes from reads.
#define ARM64_ABORT_KIND_MASK    0x3cU
#define ARM64_ABORT_TRANSLATION  4U
#define ARM64_ABORT_WRITE        (1U << 6)

// SVC ISS[15:0] is the immediate. These values form the kernel's SVC ABI.
#define ARM64_SVC_IMMEDIATE_MASK 0xffffU
#define ARM64_SVC_SYSCALL        0U
#define ARM64_SVC_SIGNAL_RETURN  2U

// MOPS ISS register operands are five-bit fields. Encoding 31 is outside
// Context.X; SET uses its source field as a value rather than a saved pointer.
#define ARM64_MOPS_REGISTER_MASK      31U
#define ARM64_MOPS_DESTINATION_SHIFT  10U
#define ARM64_MOPS_SOURCE_SHIFT       5U
#define ARM64_MOPS_SET                (1ULL << 24)
#define ARM64_MOPS_EPILOGUE           (1ULL << 18)
#define ARM64_MOPS_OPTION_SHIFT       16U
#define ARM64_MOPS_WRONG_OPTION_SHIFT 17U

// PSTATE.M selects EL0t or EL1h. Signal return retains only arithmetic NZCV
// flags, never privileged execution or interrupt-mask state. N marks MOPS
// Option B backward copies; bit 63 marks a negative remaining size in Option A.
#define ARM64_PSTATE_NZCV        0xf0000000ULL
#define ARM64_PSTATE_NEGATIVE    (1ULL << 31)
#define ARM64_SIGN_BIT           (1ULL << 63)

// AArch64 instructions occupy four bytes; stacks and frame records are 16-byte aligned.
#define ARM64_INSTRUCTION_SIZE   4U
#define ARM64_STACK_ALIGNMENT    16U

// Firmware CPU enable methods, shared by discovery and secondary startup.
#define ARM64_CPU_ENABLE_SPIN_TABLE 1U
#define ARM64_CPU_ENABLE_PSCI       2U

// Spin-table release slots hold one naturally aligned 64-bit entry address.
#define ARM64_CPU_RELEASE_ALIGNMENT 8U

// GICv2 DT interrupts contain type, relative ID, and flags. Type 1 is a PPI;
// its relative ID is 0-15. The low flags nibble selects level-high/level-low.
#define ARM64_DT_GIC_CELLS          3U
#define ARM64_DT_GIC_PPI            1U
#define ARM64_DT_GIC_TRIGGER_MASK   0xfU
#define ARM64_DT_GIC_LEVEL_HIGH     4U
#define ARM64_DT_GIC_LEVEL_LOW      8U

// The unnamed timer binding lists the nonsecure physical timer second.
#define ARM64_DT_PHYSICAL_TIMER_INDEX 1U

// Both GICv2 register regions must cover at least their first 4 KiB page.
#define ARM64_GIC_REGION_MINIMUM    0x1000U

// PL011 DR transmits bytes; FR.TXFF[5] means its transmit FIFO is full.
#define ARM64_UART_DATA             0x00U
#define ARM64_UART_FLAGS            0x18U
#define ARM64_UART_TX_FULL          (1U << 5)
#define ARM64_MMIO_REGISTER_SIZE    4U

// Console discovery must include FR, the highest register used by this port.
#define ARM64_UART_REGION_MINIMUM   (ARM64_UART_FLAGS + ARM64_MMIO_REGISTER_SIZE)

// Per-core stack reservations: IRQ dispatch survives thread retirement; the
// smaller secondary stack is used only while that core enters shared startup.
#define ARM64_INTERRUPT_STACK_SIZE 32768U
#define ARM64_SECONDARY_STACK_SIZE 16384U

// CPU feature fields: AdvSIMD is a signed nibble; FPEN must permit both ELs.
#define ARM64_PFR0_SIMD_SHIFT       20U
#define ARM64_PFR0_SIMD_SIGN_BIT    8U
#define ARM64_CPACR_FP_MASK         ARM64_CPACR_FP_ENABLE
#define ARM64_SCTLR_MOPS_ENABLE     (1ULL << 33)

// AAPCS64 x18 is platform TLS, x29 is the frame pointer, x30 is the return
// address. A frame record is a saved frame pointer followed by a saved LR.
#define ARM64_REGISTER_TLS          18U
#define ARM64_REGISTER_FRAME        29U
#define ARM64_REGISTER_LR           30U
#define ARM64_FRAME_RECORD_SIZE     16U

// PSCI CPU_ON's 64-bit function ID starts a core at a physical entry address.
#define ARM64_PSCI_CPU_ON           0xc4000003ULL

// Preserve the 20 ms scheduler fallback and the matching initial 50 Hz timer.
#define ARM64_TIMER_DEFAULT_NS       20000000
#define ARM64_TIMER_INITIAL_HZ       50U
#define ARM64_NANOSECONDS_PER_SECOND 1000000000ULL

// CNTP_CTL_EL0.ENABLE[0]=1 and IMASK[1]=0 arm an unmasked physical timer.
#define ARM64_TIMER_ENABLE          1ULL

// The first 16 privileged TLS slots are reserved by the kernel's TLS ABI.
#define ARM64_TLS_RESERVED_SLOTS    16U

struct Arm64CpuLocal {
    uintptr_t             Reserved[ARM64_TLS_RESERVED_SLOTS];
    uuid_t                Id;
    uint64_t              LastTimerTick;
    uint32_t              InterruptToken;
    _Atomic(unsigned int) UserCpuFeatures;
    uintptr_t             InterruptStackTop;
};

struct Arm64Platform {
    uint64_t     Distributor;
    uint64_t     CpuInterface;
    uint64_t     DistributorLength;
    uint64_t     CpuInterfaceLength;
    uint64_t     Uart;
    uint64_t     UartLength;
    unsigned int TimerInterrupt;
    unsigned int TimerFlags;
    unsigned int PsciConduit;
};

extern struct Arm64Platform g_arm64Platform;
extern struct Arm64CpuLocal g_arm64CpuLocals[ARM64_CPU_COUNT];

/**
 * @brief Provides the entry address of the kernel's exception vector table.
 *
 * Each CPU writes this address to VBAR_EL1 so exceptions enter the kernel's
 * assembly handlers. This symbol identifies a table of entry points, not a
 * normal C function to call. Install it before enabling interrupt delivery.
 */
__EXTERN void
Arm64Vectors(void);

/**
 * @brief Creates the initial memory mappings and enables translation and caches.
 *
 * Called on the boot CPU before shared kernel initialization uses atomic
 * operations. Maps the loader's usable memory at its physical addresses and
 * prepares a separate table for shared kernel mappings. This gives RAM the
 * normal memory settings required by atomic operations.
 *
 * @param boot Loader-provided boot information containing the physical memory map.
 * @return OS_EOK on success, or an error if the CPU's page size is unsupported,
 *         the memory ranges cannot be mapped, or translation tables cannot be created.
 */
__EXTERN oserr_t
Arm64BootMemoryInitialize(
    _In_ struct VBoot* boot);

/**
 * @brief Sets up kernel-local storage and user CPU features for the current CPU.
 *
 * Installs this CPU's kernel-local pointer in TPIDR_EL1 and prepares its
 * interrupt-handling stack. Detects supported instructions, applies the
 * required execution controls, and then publishes the features user programs
 * may use. Call on each CPU before its interrupt or scheduling paths run.
 *
 * @param coreId Kernel-assigned ID of the current CPU, less than ARM64_CPU_COUNT.
 *               This selects its storage slot; it is not a hardware affinity value.
 */
__EXTERN void
Arm64InitializeLocal(
    _In_ unsigned int coreId);

/**
 * @brief Prepares the current CPU to receive interrupts from the GIC.
 *
 * Records this CPU's hardware routing mask, disables its private device
 * interrupts, clears pending private requests, and enables software interrupts.
 * Sets priorities and enables its receiving interface. These settings belong
 * to the current CPU, so every CPU must perform this setup during startup.
 * The GIC register regions must already be mapped by platform initialization.
 */
__EXTERN void
Arm64GicInitializeCore(void);

/**
 * @brief Starts the current CPU's physical timer for scheduling.
 *
 * Records the starting counter value, enables the timer's interrupt line for
 * this CPU, and schedules the first interrupt at the initial 50 Hz rate.
 * The CPU's local storage and interrupt interface must already be ready, and
 * the physical counter frequency must be known and nonzero.
 */
__EXTERN void
Arm64TimerInitializeCore(void);

/**
 * @brief Updates elapsed thread time and asks the scheduler what should run next.
 *
 * Converts counter ticks since the previous update to nanoseconds and passes
 * them to the scheduler, which may select another thread. Sets the next timer
 * deadline, or disables the timer if there is no work or wakeup deadline.
 * Called with local IRQs masked and the interrupted context recorded by the
 * CPU's interrupt bookkeeping. The timer must already be initialized.
 *
 * @param preemptive Nonzero for a timer interrupt; zero for an explicit yield
 *                   or a software interrupt requesting a scheduling update.
 */
__EXTERN void
Arm64TimerAdvance(
    _In_ int preemptive);

/**
 * @brief Maps physical device registers for access by the kernel.
 *
 * Creates a persistent, shared kernel mapping with device memory settings,
 * without user access or instruction execution. Covers the pages containing
 * the requested range and preserves the starting offset within the first page.
 *
 * @param physical Physical address of the first device byte to access.
 * @param length Number of bytes in the device range to map.
 * @return Kernel virtual address of the requested first byte, or zero if mapping fails.
 */
__EXTERN uintptr_t
Arm64MapDevice(
    _In_ uint64_t physical,
    _In_ uint64_t length);

/**
 * @brief Saves the current CPU's SIMD and floating-point state for a thread.
 *
 * Stores all 32 SIMD registers, followed by the floating-point control and
 * status registers (FPCR and FPSR). Used when switching away from a thread so
 * another thread can use these registers without losing the saved values.
 * The CPU must already allow kernel access to these registers.
 *
 * @param buffer Writable storage of at least ARM64_FP_STATE_SIZE bytes (528 bytes).
 *               The caller owns the buffer and keeps it until the state is restored.
 */
__EXTERN void
Arm64SaveFp(
    _Out_ void* buffer);

/**
 * @brief Restores a thread's SIMD and floating-point state on the current CPU.
 *
 * Loads all 32 SIMD registers and the floating-point control and status
 * registers using the layout written by Arm64SaveFp. Used before resuming a
 * thread so it sees its own register values rather than the previous thread's.
 * The CPU must already allow kernel access to these registers.
 *
 * @param buffer Readable storage of at least ARM64_FP_STATE_SIZE bytes (528 bytes),
 *               containing saved state or the zeroed state of a new thread.
 */
__EXTERN void
Arm64RestoreFp(
    _In_ const void* buffer);

#endif //!__VALI_AARCH64_PRIVATE_H__

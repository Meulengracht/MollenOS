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

#include <arch/interrupts.h>
#include <arch/utils.h>
#include <component/cpu.h>
#include <machine.h>
#include <string.h>
#include "private.h"
#include <debug.h>

static volatile uint32_t* g_distributor;
static volatile uint32_t* g_cpuInterface;
static unsigned int       g_interruptCount;
static Spinlock_t         g_gicLock;

static void
__WriteDistributor(
    unsigned int offset,
    uint32_t value)
{
    g_distributor[offset / ARM64_GIC_REGISTER_SIZE] = value;
    __asm__ volatile("dsb sy" ::: "memory");
}

static uint32_t
__GetCoreTarget(void)
{
    uint32_t target = g_distributor[ARM64_GICD_ITARGETSR / ARM64_GIC_REGISTER_SIZE];

    // Each byte names this CPU for one banked interrupt. Some implementations
    // leave individual bytes zero, so combine all four before selecting a byte.
    target |= target >> 16;
    target |= target >> 8;
    return target & ARM64_GIC_BYTE_MASK;
}

static uint32_t
__GetInterruptGroup(void)
{
    uint32_t capabilities = g_distributor[ARM64_GICD_TYPER / ARM64_GIC_REGISTER_SIZE];

    // The nonsecure view uses firmware-owned Group 1 when security is present;
    // otherwise all interrupts belong to Group 0.
    return (capabilities & ARM64_GIC_TYPER_SECURITY) ? ARM64_GIC_ALL_BITS : 0;
}

void
Arm64GicInitializeCore(void)
{
    PlatformCpuCoreBlock_t* core = CpuCorePlatformBlock(CpuCoreCurrent());
    unsigned int            offset;

    // ITARGETSR0 describes this interface's target bit; MPIDR affinity is not
    // a GIC target number (notably on BCM2712).
    core->GicTarget = __GetCoreTarget();
    __WriteDistributor(ARM64_GICD_ICENABLER, ARM64_GIC_PPI_MASK);
    __WriteDistributor(ARM64_GICD_ICPENDR, ARM64_GIC_ALL_BITS);
    
    // Without security extensions, use Group 0 with EnableGrp0. With
    // security extensions, the nonsecure view enables firmware-owned Group 1.
    __WriteDistributor(ARM64_GICD_IGROUPR, __GetInterruptGroup());
    for (offset = ARM64_GICD_IPRIORITYR;
         offset < ARM64_GICD_IPRIORITYR + ARM64_GIC_SPI_BASE;
         offset += ARM64_GIC_REGISTER_SIZE) {
        __WriteDistributor(offset, ARM64_GIC_DEFAULT_PRIORITIES);
    }
    __WriteDistributor(ARM64_GICD_ISENABLER, ARM64_GIC_SGI_MASK);
   
    // Accept every priority and use no subpriority split. IRQ nesting remains
    // disabled by DAIF.I rather than by changing this threshold in handlers.
    g_cpuInterface[ARM64_GICC_PMR / ARM64_GIC_REGISTER_SIZE] = ARM64_GIC_BYTE_MASK;
    g_cpuInterface[ARM64_GICC_BPR / ARM64_GIC_REGISTER_SIZE] = 0;
    g_cpuInterface[ARM64_GICC_CTLR / ARM64_GIC_REGISTER_SIZE] = ARM64_GIC_ENABLE;

    __asm__ volatile("dsb sy\nisb" ::: "memory");
}

static void
__InitializeSharedInterrupts(
    _In_ uint32_t target)
{
    unsigned int line;
    unsigned int offset;
    uint32_t     group = __GetInterruptGroup();

    for (line = ARM64_GIC_SPI_BASE; line < g_interruptCount; line += ARM64_GIC_BITS_PER_REGISTER) {
        offset = (line / ARM64_GIC_BITS_PER_REGISTER) * ARM64_GIC_REGISTER_SIZE;
        __WriteDistributor(ARM64_GICD_ICENABLER + offset, ARM64_GIC_ALL_BITS);
        __WriteDistributor(ARM64_GICD_ICPENDR + offset, ARM64_GIC_ALL_BITS);
        __WriteDistributor(ARM64_GICD_IGROUPR + offset, group);
    }
    
    for (line = ARM64_GIC_SPI_BASE; line < g_interruptCount; line += ARM64_GIC_BYTES_PER_REGISTER) {
        __WriteDistributor(ARM64_GICD_IPRIORITYR + line, ARM64_GIC_DEFAULT_PRIORITIES);
        __WriteDistributor(ARM64_GICD_ITARGETSR + line, target);
    }
    
    // Clearing both configuration bits selects level triggering for each SPI.
    for (line = ARM64_GIC_SPI_BASE; line < g_interruptCount; line += ARM64_GIC_CONFIG_PER_REGISTER) {
        offset = (line / ARM64_GIC_CONFIG_PER_REGISTER) * ARM64_GIC_REGISTER_SIZE;
        __WriteDistributor(ARM64_GICD_ICFGR + offset, 0);
    }
}

oserr_t
PlatformInterruptInitialize(void)
{
    uint32_t target;
    uint32_t capabilities;

    // The distributor controls interrupt routing and shared device lines;
    // the CPU interface lets this core receive and acknowledge interrupts.
    // Map both register regions before attempting to access the controller.
    g_distributor = (void*)Arm64MapDevice(g_arm64Platform.Distributor, g_arm64Platform.DistributorLength);
    g_cpuInterface = (void*)Arm64MapDevice(g_arm64Platform.CpuInterface, g_arm64Platform.CpuInterfaceLength);
    // Without either mapping, we cannot safely configure or use the controller.
    if (!g_distributor || !g_cpuInterface) {
        return OS_EOOM;
    }
    
    // Later configuration can come from multiple CPUs. Prepare its lock, then
    // stop distributor delivery so interrupts cannot use half-configured state.
    SpinlockConstruct(&g_gicLock);
    __WriteDistributor(ARM64_GICD_CTLR, 0);

    // TYPER reports the number of 32-interrupt groups minus one, not a direct
    // interrupt count. Convert it to the number of IDs exposed by the hardware.
    capabilities = g_distributor[ARM64_GICD_TYPER / ARM64_GIC_REGISTER_SIZE];
    g_interruptCount = ((capabilities & ARM64_GIC_TYPER_LINES_MASK) + 1) * ARM64_GIC_BITS_PER_REGISTER;
    // Stay within the kernel's interrupt tables. In particular, GIC IDs
    // 1020-1023 are special responses, not device interrupts we can register.
    if (g_interruptCount > MAX_SUPPORTED_INTERRUPTS) {
        g_interruptCount = MAX_SUPPORTED_INTERRUPTS;
    }

    // Initially route shared device interrupts to this boot CPU. Each target
    // register holds four one-byte CPU masks; multiplication repeats this
    // core's hardware target mask in all four bytes, without assuming its ID.
    target = __GetCoreTarget() * ARM64_GIC_REPEAT_BYTE;

    // Start shared lines disabled, clear old pending requests, and assign
    // their group, default priority, CPU target, and level-triggered mode.
    // Drivers can later choose their required trigger mode and enable a line.
    __InitializeSharedInterrupts(target);

    // Prepare this CPU's private interrupts and receiving interface before
    // allowing the distributor to deliver interrupts again. Other CPUs perform
    // their own local initialization when they start.
    Arm64GicInitializeCore();
    __WriteDistributor(ARM64_GICD_CTLR, ARM64_GIC_ENABLE);

    DEBUG("GIC distributor=%llx cpu=%llx type=%x control=%x target=%x",
        g_arm64Platform.Distributor, g_arm64Platform.CpuInterface,
        capabilities, g_distributor[ARM64_GICD_CTLR / ARM64_GIC_REGISTER_SIZE], target);
    return OS_EOK;
}

void
InterruptSetMode(
    int mode)
{
    (void)mode;
}

oserr_t
InterruptResolve(
    _In_  DeviceInterrupt_t* interrupt,
    _In_  unsigned int       flags,
    _Out_ uuid_t*            tableIndex)
{
    if (flags & INTERRUPT_MSI) {
        return OS_ENOTSUPPORTED;
    }
    
    if (flags & INTERRUPT_SOFT) {
        int candidates[ARM64_GIC_ALLOCATABLE_SGI_COUNT];

        for (unsigned int i = 0; i < ARM64_GIC_ALLOCATABLE_SGI_COUNT; i++) {
            candidates[i] = i + ARM64_GIC_ALLOCATABLE_SGI_BASE;
        }
        
        *tableIndex = InterruptGetLeastLoaded(
            candidates,
            ARM64_GIC_ALLOCATABLE_SGI_COUNT
        );
        return *tableIndex == UUID_INVALID ? OS_EOOM : OS_EOK;
    }
    
    if (interrupt->Line < (int)ARM64_GIC_PPI_BASE || interrupt->Line >= (int)g_interruptCount) {
        return OS_EINVALPARAMS;
    }
    *tableIndex = interrupt->Line;
    return OS_EOK;
}

oserr_t
InterruptConfigure(
    _In_ SystemInterrupt_t* interrupt,
    _In_ int                enable)
{
    unsigned int line = interrupt->Line;
    unsigned int shift;
    unsigned int offset;
    uint32_t     configuration;

    // Software interrupts use the CPU-to-CPU interrupt mechanism, not a device
    // trigger setting. Their enable state is established during CPU startup,
    // so there is no device line to reconfigure here.
    if (interrupt->Flags & INTERRUPT_SOFT) {
        return OS_EOK;
    }

    // IDs below the private-device range are reserved for software interrupts.
    // IDs at or above the discovered count have no usable hardware line.
    // Reject both before calculating offsets into the distributor registers.
    if (line < ARM64_GIC_PPI_BASE || line >= g_interruptCount) {
        return OS_EINVALPARAMS;
    }

    // Different CPUs can configure interrupts stored in the same register.
    // Hold the lock across the whole update so they cannot overwrite each
    // other's settings. Mask local IRQs while holding it so an interrupt on
    // this CPU cannot interrupt the owner and then wait for the same lock.
    SpinlockAcquireIrq(&g_gicLock);

    // ICENABLER has one bit per interrupt, with 32 interrupts in each word.
    // Division selects the word; multiplication converts its index to a byte
    // offset; the remainder selects this interrupt's bit within that word.
    // Writing a one disables that line; zero bits leave all other lines alone.
    // This is a clear command, not an ordinary register value to read/modify.
    // Disable delivery before changing the trigger mode. It does not clear a
    // pending request or finish an already-active interrupt. The write helper's
    // barrier completes the disable write before we change the configuration.
    offset = (line / ARM64_GIC_BITS_PER_REGISTER) * ARM64_GIC_REGISTER_SIZE;
    __WriteDistributor(ARM64_GICD_ICENABLER + offset, 1U << (line % ARM64_GIC_BITS_PER_REGISTER));

    // Only shared peripheral interrupts get their trigger mode changed here.
    // Private peripheral interrupts belong to the current CPU; leave their
    // firmware/platform-established trigger settings unchanged in this path.
    if (line >= ARM64_GIC_SPI_BASE) {
        // ICFGR stores sixteen two-bit interrupt settings in each 32-bit word.
        // Select the word and the position of this line's two-bit field.
        offset = ARM64_GICD_ICFGR + (line / ARM64_GIC_CONFIG_PER_REGISTER) * ARM64_GIC_REGISTER_SIZE;
        shift = (line % ARM64_GIC_CONFIG_PER_REGISTER) * ARM64_GIC_CONFIG_BITS;

        // Unlike the enable/disable commands, ICFGR stores a complete register
        // value. Preserve the other fifteen lines and clear only this field.
        // Its low bit is reserved and stays zero; its high bit selects edge
        // triggering when set, or level triggering when clear.
        configuration = g_distributor[offset / ARM64_GIC_REGISTER_SIZE] & ~(ARM64_GIC_CONFIG_MASK << shift);

        // The kernel's trigger-mode flag means level-triggered: the device
        // keeps its request asserted until serviced. With no such flag, select
        // edge-triggered delivery, which records a change in the input signal.
        if (!(interrupt->AcpiConform & INTERRUPT_ACPICONFORM_TRIGGERMODE)) {
            configuration |= ARM64_GIC_CONFIG_EDGE << shift;
        }

        // Write the preserved word with this line's new setting. The helper's
        // barrier completes the configuration write before the line is enabled.
        __WriteDistributor(offset, configuration);
    }

    // ISENABLER is the matching set command: writing one enables this line;
    // zero bits do not disable or otherwise change neighboring lines. Recompute
    // the enable-word offset because the trigger update used a different bank.
    // If enable is false, leave the line disabled by the earlier clear command.
    if (enable) {
        offset = (line / ARM64_GIC_BITS_PER_REGISTER) * ARM64_GIC_REGISTER_SIZE;
        __WriteDistributor(ARM64_GICD_ISENABLER + offset, 1U << (line % ARM64_GIC_BITS_PER_REGISTER));
    }

    // Make the controller available to other CPUs, then restore this CPU's
    // previous IRQ mask rather than unconditionally enabling interrupts.
    SpinlockReleaseIrq(&g_gicLock);
    return OS_EOK;
}

void
Arm64EnableTimerInterrupt(void)
{
    __WriteDistributor(ARM64_GICD_ISENABLER, 1U << g_arm64Platform.TimerInterrupt);
    
    DEBUG("GIC core=%u timer=%u enabled=%x cpu-control=%x priority-mask=%x",
        ArchGetProcessorCoreId(), g_arm64Platform.TimerInterrupt,
        g_distributor[ARM64_GICD_ISENABLER / ARM64_GIC_REGISTER_SIZE],
        g_cpuInterface[ARM64_GICC_CTLR / ARM64_GIC_REGISTER_SIZE],
        g_cpuInterface[ARM64_GICC_PMR / ARM64_GIC_REGISTER_SIZE]);
}

Context_t*
Arm64HandleInterrupt(
    _In_ Context_t* context)
{
    struct Arm64CpuLocal* local = &g_arm64CpuLocals[ArchGetProcessorCoreId()];
    uint32_t              token;
    uint32_t              line;

    // Reading the Interrupt Acknowledge Register (IAR) is not just a status
    // check: it claims the highest-priority eligible pending interrupt for
    // this CPU and marks it active. Read once and keep the returned value for
    // the matching end-of-interrupt write after its handler has finished.
    token = g_cpuInterface[ARM64_GICC_IAR / ARM64_GIC_REGISTER_SIZE];

    // Bits 9:0 identify the interrupt to dispatch. For a software-generated
    // interrupt, the token also identifies its sending CPU; those extra bits
    // are not part of the interrupt-table index and must not be discarded.
    line = token & ARM64_GIC_INTERRUPT_ID_MASK;

    // IDs 1020-1023 are special controller responses, not interrupt handlers.
    // For example, 1023 means there is no eligible interrupt to claim. Such a
    // response does not need an end-of-interrupt write; resume the same context
    // without saving this token or looking up a device handler.
    if (line >= ARM64_GIC_SPECIAL_ID_BASE) {
        return context;
    }

    // Save the full token on the receiving CPU so InterruptsAcknowledge can
    // return it unchanged to EOIR, including the sender bits for software
    // interrupts. IRQ nesting is disabled in this port, so another handler
    // cannot replace this CPU's token before the current one is completed.
    local->InterruptToken = token;

    // INTERRUPT_LAPIC is the shared kernel's scheduler-interrupt name. Here
    // it denotes a software-generated request to reconsider which thread runs,
    // not an x86 controller or the physical timer's interrupt line. Handle it
    // directly because it does not use the registered device-handler list.
    if (line == INTERRUPT_LAPIC) {
        // Register the interrupted context before scheduling, so the scheduler
        // can save the outgoing thread and choose a different context to resume.
        CpuCoreEnterInterrupt(context, 0);

        // Account time already spent running and update the next timer deadline.
        // Zero selects request-driven scheduling rather than timer preemption.
        Arm64TimerAdvance(0);

        // Complete the claimed GIC interrupt even if scheduling selected another
        // thread. EOIR must receive the saved token before that thread resumes.
        InterruptsAcknowledge(INTERRUPT_LAPIC, INTERRUPT_LAPIC);

        // Finish interrupt bookkeeping and return the selected context, which
        // need not belong to the thread that received this scheduling request.
        return CpuCoreExitInterrupt(context, 0);
    }

    // Other IDs use the shared interrupt-table dispatcher, including the
    // physical timer's registered handler. It performs context bookkeeping,
    // calls the registered handlers, and completes the GIC interrupt through
    // InterruptsAcknowledge; doing those steps here as well would duplicate them.
    return InterruptHandle(context, line);
}

void
InterruptsAcknowledge(
    _In_ int      source,
    _In_ uint32_t tableIndex)
{
    struct Arm64CpuLocal* local = &g_arm64CpuLocals[ArchGetProcessorCoreId()];

    // These arguments belong to the shared interrupt API. source identifies
    // the registered handler's source and can be INTERRUPT_NONE if no handler
    // claims the request; tableIndex identifies the software dispatch entry.
    // Neither tells the GIC exactly what this CPU claimed. GICv2 requires the
    // token read from IAR, including the sending CPU for a software interrupt.
    // The table index lacks those sender bits, and completing a claimed GIC
    // interrupt is required even when no registered handler handled it.
    // Use the receiving CPU's saved token instead of reconstructing it from
    // these arguments. IRQ nesting is disabled, so that token is still current.
    (void)source;
    (void)tableIndex;

    // Finish the handler's earlier memory and device-register writes before
    // telling the interrupt controller that handling is complete. For a
    // level-triggered device, its handler must clear the device's request;
    // writing EOIR does not clear the device's own interrupt-status register.
    __asm__ volatile("dsb sy" ::: "memory");

    // EOIR is the CPU interface's end-of-interrupt register. Return the full
    // saved IAR token unchanged so the GIC completes the correct interrupt,
    // including the correct sender for a software-generated request. In this
    // port's combined EOI mode, the write releases its running priority and
    // clears its active state; a request still pending can then be delivered
    // again when priority and CPU interrupt masking allow it.
    g_cpuInterface[ARM64_GICC_EOIR / ARM64_GIC_REGISTER_SIZE] = local->InterruptToken;

    // Complete the EOIR write before returning to exception exit, which may
    // resume the interrupted thread or a different thread chosen by scheduling.
    __asm__ volatile("dsb sy" ::: "memory");
}

uint32_t
InterruptsGetPriority(void)
{
    return 0;
}

void
InterruptsSetPriority(
    _In_ uint32_t priority)
{
    // Generic priorities in Vali are vector numbers, not GIC PMR values. Handler
    // nesting remains disabled through DAIF, with a fixed GIC PMR threshold.
    (void)priority;
}

irqstate_t
InterruptSaveState(void)
{
    uint64_t state;

    __asm__ volatile("mrs %0, daif" : "=r"(state));
    return state;
}

irqstate_t
InterruptDisable(void)
{
    irqstate_t state = InterruptSaveState();

    // DAIFSet immediate bit one masks IRQs without masking other exceptions.
    __asm__ volatile("msr daifset, %0" :: "i"(ARM64_DAIF_IRQ) : "memory");
    return state;
}

irqstate_t
InterruptEnable(void)
{
    irqstate_t state = InterruptSaveState();

    // DAIFClr immediate bit one unmasks IRQs only.
    __asm__ volatile("msr daifclr, %0" :: "i"(ARM64_DAIF_IRQ) : "memory");
    return state;
}

irqstate_t
InterruptRestoreState(
    _In_ irqstate_t state)
{
    irqstate_t previous = InterruptSaveState();

    __asm__ volatile("msr daif, %0" :: "r"((uint64_t)state) : "memory");
    return previous;
}

int
InterruptIsDisabled(void)
{
    return (InterruptSaveState() & ARM64_DAIF_IRQ_MASK) != 0;
}

oserr_t
ArchProcessorSendInterrupt(
    _In_ uuid_t coreId,
    _In_ uuid_t interruptId)
{
    SystemCpuCore_t* core;
    unsigned int     line = interruptId & ARM64_INTERRUPT_VECTOR_MASK;

    if (coreId >= ARM64_CPU_COUNT || line >= ARM64_GIC_PPI_BASE) {
        return OS_EINVALPARAMS;
    }
    
    core = GetProcessorCore(coreId);
    if (!core || !CpuCorePlatformBlock(core)->GicTarget) {
        return OS_ENOENT;
    }
    
    // Publish shared stores before the SGI makes the target CPU observe them.
    __asm__ volatile("dsb ishst" ::: "memory");
    __WriteDistributor(
        ARM64_GICD_SGIR,
        (CpuCorePlatformBlock(core)->GicTarget << ARM64_GIC_SGI_TARGET_SHIFT) | line
    );
    return OS_EOK;
}

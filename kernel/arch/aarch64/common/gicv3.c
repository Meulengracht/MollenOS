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
 * GICv3 Interrupt Controller (non-secure, physical interrupts)
 *
 * Preconditions: Firmware must allow ICC_* register access at EL1. When security
 * is enabled, firmware must also have put the kernel's interrupts in Group 1
 * Non-secure, as we cannot change the group of secure interrupts from EL1.
 * All register frames are mapped through Arm64MapDevice, so the registers are
 * never mapped as Normal memory as well.
 */

#include <component/cpu.h>
#include <debug.h>
#include <machine.h>
#include "private.h"
#include "device.h"
#include "gic.h"
#include "gicv3-registers.h"

#define GIC_FRAME_SIZE       0x10000U
#define GIC_RD_SIZE          (2U * GIC_FRAME_SIZE)
#define GIC_PIDR2            0xffe8U
#define GICD_IROUTER         0x6000U
#define GICD_RWP             (1U << 31)
#define GICD_RSS             (1U << 26)
#define GICD_DS              (1U << 6)
#define GICD_ARE             (1U << 4)
#define GICR_TYPER           0x8U
#define GICR_WAKER           0x14U
#define GICR_RWP             (1U << 3)
#define GICR_SLEEP           (1U << 1)
#define GICR_CHILDREN_ASLEEP (1U << 2)
#define GICR_LAST            (1ULL << 4)
#define GICR_VLPIS           (1ULL << 1)
#define ICC_CTLR_RSS         (1ULL << 18)
#define ICC_CTLR_EOIMODE     (1ULL << 1)
#define GIC_ICACTIVER        0x380U

static uintptr_t    g_distributor;
static unsigned int g_interruptCount;
static int          g_needsRangeSelector;

static oserr_t
__WaitClear(
    _In_ uintptr_t    base,
    _In_ unsigned int offset,
    _In_ uint32_t     mask)
{
    uint64_t start;
    uint64_t now;
    uint64_t frequency;

    // We cannot use the scheduler for timeouts this early, so poll the physical
    // counter instead. It works before the GIC is set up and keeps counting with
    // IRQs masked. The subtraction below also works if the counter wraps, and
    // one second is plenty of time for a register update to finish.
    GIC_READ_SYSTEM(cntfrq_el0, frequency);
    if (!frequency) {
        return OS_ENOTSUPPORTED;
    }

    GIC_READ_SYSTEM(cntpct_el0, start);
    while (__GicRead32(base, offset) & mask) {
        GIC_READ_SYSTEM(cntpct_el0, now);
        if (now - start >= frequency) {
            ERROR("GICv3 timeout: base=%llx register=%x mask=%x", (uint64_t)base, offset, mask);
            return OS_EUNKNOWN;
        }
        GIC_RELAX();
    }
    return OS_EOK;
}

static uint32_t
__AffinityValue(
    _In_ uint64_t affinity)
{
    // MPIDR has Aff3 at [39:32] with a gap at [31:24]. GICR_TYPER puts Aff3
    // right above Aff2, so move it down to [31:24] to compare the two.
    return (uint32_t)(affinity & 0xffffff) | (uint32_t)((affinity >> 8) & 0xff000000);
}

static oserr_t
__MatchRedistributor(
    _In_  uint64_t                physical,
    _In_  uint64_t                remaining,
    _In_  PlatformCpuCoreBlock_t* expected,
    _Out_ uint64_t*               step,
    _Out_ int*                    last)
{
    uintptr_t mapped;
    uint64_t  type;

    if (remaining < GIC_RD_SIZE) {
        return OS_EINVALPARAMS;
    }

    mapped = Arm64MapDevice(physical, GIC_RD_SIZE);
    if (!mapped) {
        return OS_EOOM;
    }

    if ((__GicRead32(mapped, GIC_PIDR2) & 0xf0) != 0x30) {
        return OS_ENOTSUPPORTED;
    }

    type = __GicRead64(mapped, GICR_TYPER);
    *step = (type & GICR_VLPIS) ? 2 * GIC_RD_SIZE : GIC_RD_SIZE;
    *last = (type & GICR_LAST) != 0;
    if (*step > remaining) {
        return OS_EINVALPARAMS;
    }

    // Extended PPIs (GICR_TYPER.PPInum) need extra interrupt tables and SGI
    // frame registers that we do not support. Fail here, otherwise firmware
    // could leave IDs enabled that our dispatcher cannot handle.
    if ((type >> 27) & 0x1f) {
        return OS_ENOTSUPPORTED;
    }
    
    if (expected && __AffinityValue(expected->Affinity) != (uint32_t)(type >> 32)) {
        return OS_EINVALPARAMS;
    }

    for (unsigned int i = 0; i < (unsigned int)GetMachine()->Processor.NumberOfCores; i++) {
        PlatformCpuCoreBlock_t* core = CpuCorePlatformBlock(GetProcessorCore(i));
        
        if (__AffinityValue(core->Affinity) != (uint32_t)(type >> 32)) {
            continue;
        }
        
        if (core->Gic.Redistributor) {
            return OS_EINVALPARAMS;
        }
        
        core->Gic.RedistributorPhysical = physical;
        core->Gic.Redistributor = mapped;
        break;
    }
    return OS_EOK;
}

static oserr_t
__DiscoverRedistributors(void)
{
    const struct Arm64GicDescription* gic = &g_arm64Platform.Gic;
    uint64_t                          step;
    int                               last;
    oserr_t                           status;

    // The ACPI regions can contain frames for disabled CPUs, and the frames do
    // not have to be in MADT CPU order. Walk each region within its own length
    // and stop early at the frame marked Last. A mapping failure stops boot.
    // The device mappings are never handed out as normal RAM, not even when we
    // fail here.
    for (unsigned int i = 0; i < gic->RegionCount; i++) {
        uint64_t offset = 0;
        
        do {
            status = __MatchRedistributor(
                gic->Regions[i].Base + offset,
                gic->Regions[i].Length - offset,
                NULL,
                &step,
                &last
            );
            if (status != OS_EOK) {
                return status;
            }

            offset += step;
        } while (!last && offset < gic->Regions[i].Length);
    }
    
    for (unsigned int i = 0; i < (unsigned int)GetMachine()->Processor.NumberOfCores; i++) {
        PlatformCpuCoreBlock_t* core = CpuCorePlatformBlock(GetProcessorCore(i));
        
        if (!gic->RegionCount) {
            status = __MatchRedistributor(
                core->Gic.RedistributorPhysical,
                GIC_RD_SIZE,
                core,
                &step,
                &last
            );
            if (status != OS_EOK) {
                return status;
            }
        }

        if (!core->Gic.Redistributor) {
            ERROR("GICv3 has no redistributor for CPU affinity %llx", core->Affinity);
            return OS_ENOENT;
        }

        if ((core->Affinity & 0xff) >= 16) {
            g_needsRangeSelector = 1;
        }
    }
    return OS_EOK;
}

static oserr_t
__InitializeCore(void)
{
    PlatformCpuCoreBlock_t* core = CpuCorePlatformBlock(CpuCoreCurrent());
    uintptr_t               rd = core->Gic.Redistributor;
    uintptr_t               sgi = rd + GIC_FRAME_SIZE;
    uint64_t                value;
    uint32_t                control;
    unsigned int            offset;
    oserr_t                 status;

    // Preconditions: The redistributor of the current CPU must already be
    // discovered, and the CPU must not be marked ready for IPIs yet. This is
    // called on every CPU, including secondary CPUs, and only touches registers
    // local to the calling CPU.

    // Check that ID_AA64PFR0.GIC reports the system register interface before
    // touching any ICC register. EL2/EL3 must allow access, as we cannot undo
    // their traps from EL1.
    GIC_READ_SYSTEM(id_aa64pfr0_el1, value);
    if (!((value >> 24) & 0xf)) {
        return OS_ENOTSUPPORTED;
    }
    
    // Enable the system register interface at EL1 and disable the legacy
    // IRQ/FIQ bypass. Other SRE bits are kept. Read the value back afterwards,
    // since a higher exception level can block the change, and then the ICC
    // registers would not be usable.
    GIC_READ_SYSTEM(icc_sre_el1, value);
    GIC_WRITE_SYSTEM(icc_sre_el1, value | 7);
    GIC_BARRIER();
    GIC_READ_SYSTEM(icc_sre_el1, value);
    if (!(value & 1)) {
        ERROR("GICv3 system-register interface is unavailable at EL1");
        return OS_ENOTSUPPORTED;
    }

    // Disable Group 1 on this CPU interface while we set up the redistributor
    // and priorities. The distributor side is enabled separately in
    // __Initialize, and both must be enabled before any SPI is delivered.
    GIC_WRITE_SYSTEM(icc_igrpen1_el1, 0);
    GIC_BARRIER();

    // To send SGIs to CPUs with Aff0 >= 16, the sending CPU interface must
    // support the range selector (ICC_CTLR_EL1.RSS). The distributor RSS bit
    // is checked in __Initialize, and we need both.
    GIC_READ_SYSTEM(icc_ctlr_el1, value);
    if (g_needsRangeSelector && !(value & ICC_CTLR_RSS)) {
        return OS_ENOTSUPPORTED;
    }
    
    // With EOImode=0, a write to EOIR1 both drops the running priority and
    // deactivates the interrupt. The common interrupt code only writes EOIR1
    // and never DIR, so we need this mode. Read it back to make sure the CPU
    // interface accepted it.
    GIC_WRITE_SYSTEM(icc_ctlr_el1, value & ~ICC_CTLR_EOIMODE);
    GIC_BARRIER();
    GIC_READ_SYSTEM(icc_ctlr_el1, value);
    if (value & ICC_CTLR_EOIMODE) {
        return OS_ENOTSUPPORTED;
    }

    // The redistributor may still have ProcessorSleep set after reset or from
    // firmware. Clear it to wake up the SGI/PPI frame of this CPU, and then wait
    // for ChildrenAsleep to clear, as the wake up does not happen instantly.
    control = __GicRead32(rd, GICR_WAKER);
    __GicWrite32(rd, GICR_WAKER, control & ~GICR_SLEEP);
    status = __WaitClear(rd, GICR_WAKER, GICR_CHILDREN_ASLEEP);
    if (status != OS_EOK) {
        return status;
    }

    // Disable LPIs (GICR_CTLR.EnableLPIs, bit 0) in case firmware left them on.
    // We have no LPI property table or ITS support, and the dispatcher cannot
    // handle LPI IDs. Wait until both EnableLPIs and RWP read as zero before
    // we start on the SGI/PPI registers.
    control = __GicRead32(rd, ARM64_GICD_CTLR);
    __GicWrite32(rd, ARM64_GICD_CTLR, control & ~1U);
    status = __WaitClear(rd, ARM64_GICD_CTLR, GICR_RWP | 1U);
    if (status != OS_EOK) {
        return status;
    }

    // SGIs and PPIs are private to this CPU, and their 32 enable bits are in a
    // single register in the SGI frame. Disable all of them before changing
    // anything else, so interrupts left enabled by firmware are not delivered
    // once we enable Group 1.
    __GicWrite32(sgi, ARM64_GICD_ICENABLER, 0xffffffff);

    // Wait for RWP to clear, so the disable has completed before we change the
    // rest of the configuration.
    status = __WaitClear(rd, ARM64_GICD_CTLR, GICR_RWP);
    if (status != OS_EOK) {
        return status;
    }

    // ICC_IAR1_EL1 acknowledges Group 1 interrupts. With security enabled,
    // firmware must already have put our interrupts in Group 1 Non-secure,
    // as EL1 cannot change secure assignments. With DS=1, IGROUPR selects
    // Group 1 directly.
    // Clear the pending and active bits left by firmware. A level-triggered
    // device still asserting its line will become pending again.
    __GicWrite32(sgi, ARM64_GICD_IGROUPR, 0xffffffff);
    __GicWrite32(sgi, ARM64_GICD_ICPENDR, 0xffffffff);
    __GicWrite32(sgi, GIC_ICACTIVER, 0xffffffff);
    
    // Each word holds four priority bytes. Use the same default priority as the
    // distributor, so private and shared interrupts start out equal until a
    // driver changes a specific line.
    for (offset = 0; offset < 32; offset += 4) {
        __GicWrite32(sgi, ARM64_GICD_IPRIORITYR + offset, ARM64_GIC_DEFAULT_PRIORITIES);
    }

    // We require the physical timer from GTDT to be level-triggered, so set its
    // PPI up for that here. Other PPIs keep their trigger mode until a driver
    // configures them.
    // ICFGR has two bits per interrupt (16 IDs per word). Clear only the timer's
    // trigger bit. Some implementations fix the trigger mode in hardware, so
    // read it back and fail if the timer cannot be set to level-triggered.
    offset = ARM64_GICD_ICFGR + (g_arm64Platform.TimerInterrupt / 16) * 4;
    control = __GicRead32(sgi, offset);
    control &= ~(2U << ((g_arm64Platform.TimerInterrupt % 16) * 2));
    __GicWrite32(sgi, offset, control);
    if (__GicRead32(sgi, offset) != control) {
        return OS_ENOTSUPPORTED;
    }
    
    // Enable the SGIs used for software interrupts. PPIs stay disabled here.
    // The timer code enables its PPI after installing the handler, so we cannot
    // receive a timer interrupt before there is a handler for it.
    __GicWrite32(sgi, ARM64_GICD_ISENABLER, ARM64_GIC_SGI_MASK);

    // Let every priority through the CPU interface. BPR1=0 selects the finest
    // Group 1 priority grouping. We do not use priority grouping for nested
    // IRQs, as IRQs stay masked on the CPU while a handler runs.
    GIC_WRITE_SYSTEM(icc_pmr_el1, 0xff);
    GIC_WRITE_SYSTEM(icc_bpr1_el1, 0);
    GIC_BARRIER();
    
    // Enable Group 1 on this CPU interface last, once the private lines and the
    // EOI mode are set up. IRQs are still only delivered when the distributor
    // has Group 1 enabled and the CPU has IRQs unmasked.
    GIC_WRITE_SYSTEM(icc_igrpen1_el1, 1);
    GIC_BARRIER();
    return OS_EOK;
}

static oserr_t
__Initialize(
    _Out_ unsigned int* count)
{
    uint32_t     type;
    uint32_t     control;
    uint32_t     groupEnable;
    unsigned int line;
    unsigned int offset;
    uint64_t     affinity = CpuCorePlatformBlock(CpuCoreCurrent())->Affinity;
    oserr_t      status;

    // Preconditions: Called once on the boot CPU with IRQs masked, before any
    // secondary CPUs are started. The distributor holds the shared peripheral
    // interrupt (SPI) state for the whole machine, so running this again on
    // another CPU would reset interrupts that are already in use. The private
    // interrupt state of each CPU is set up by __InitializeCore.

    // The distributor is a single 64 KiB register frame. It must be mapped as
    // Device memory, so accesses go straight to the controller without any
    // caching or speculative reads.
    g_distributor = Arm64MapDevice(g_arm64Platform.Gic.Distributor, GIC_FRAME_SIZE);
    if (!g_distributor) {
        return OS_EOOM;
    }

    // Make sure the hardware agrees with firmware that this is a GICv3
    // (PIDR2.ArchRev[7:4] == 3) before writing any registers. Other revisions
    // may need a different setup sequence.
    if ((__GicRead32(g_distributor, GIC_PIDR2) & 0xf0) != 0x30) {
        return OS_ENOTSUPPORTED;
    }

    // TYPER.ESPI (bit 8) means the extended SPI range (IDs from 4096) exists.
    // Those use separate registers and IDs that we do not support. Fail here,
    // otherwise firmware could leave some of them enabled that neither this
    // setup nor the dispatcher knows about.
    type = __GicRead32(g_distributor, ARM64_GICD_TYPER);
    if (type & (1U << 8)) {
        return OS_ENOTSUPPORTED;
    }

    // TYPER.ITLinesNumber[4:0] is the number of 32-ID blocks minus one,
    // including the private IDs 0-31. The result is the upper limit of IDs,
    // and says nothing about how many devices are connected. IDs 1020-1023
    // are special values returned on acknowledge, so cap the count below them,
    // even when TYPER covers 1024 IDs.
    g_interruptCount = ((type & 31) + 1) * 32;
    if (g_interruptCount > ARM64_GIC_SPECIAL_ID_BASE) {
        g_interruptCount = ARM64_GIC_SPECIAL_ID_BASE;
    }

    // Find the redistributor frame of each CPU by matching affinities, as the
    // frames can be in a different order than the CPU list. This also checks
    // that every CPU has a frame, so we catch missing hardware before any
    // secondary CPUs are started.
    status = __DiscoverRedistributors();
    if (status != OS_EOK) {
        return status;
    }

    // An SGI request has a 16-bit target list, so CPUs with Aff0 >= 16 can only
    // be reached through the Range Selector field. That needs RSS support in
    // the distributor (checked here) and in the sending CPU interface (checked
    // in __InitializeCore).
    if (g_needsRangeSelector && !(type & GICD_RSS)) {
        return OS_ENOTSUPPORTED;
    }

    // Disable Group 1 while changing routing and resetting SPIs. Keep DS and
    // the other control bits, as the security setup must stay unchanged.
    // DS=1 (security disabled): Clear Group 1's enable bit (1). Leave Group 0's
    //   enable bit (0) as is.
    // DS=0 (non-secure view): Bits 0 and 1 enable Group 1 without and with
    //   affinity routing. Clear both.
    control = __GicRead32(g_distributor, ARM64_GICD_CTLR);
    groupEnable = (control & GICD_DS) ? 2 : 3;
    control &= ~groupEnable;
    __GicWrite32(g_distributor, ARM64_GICD_CTLR, control);

    // The distributor can still be busy with the write after the MMIO access
    // returns, and a barrier on the CPU does not wait for that. Wait for RWP
    // (Register Write Pending) to clear before we touch ARE.
    status = __WaitClear(g_distributor, ARM64_GICD_CTLR, GICD_RWP);
    if (status != OS_EOK) {
        return status;
    }

    // With DS=1, ARE must not be changed while Group 0 is enabled, so firmware
    // must have disabled Group 0 if it gives us a GIC with ARE clear. If ARE is
    // already set, nothing changes and Group 0 can stay enabled. We do not
    // disable Group 0 ourselves just to make this work.
    if ((control & GICD_DS) && (control & 1) && !(control & GICD_ARE)) {
        return OS_ENOTSUPPORTED;
    }

    // Enable affinity routing (ARE), so SPIs are routed by CPU affinity through
    // IROUTER instead of the legacy 8-CPU target masks. Read it back after, as
    // the IROUTER values we write later have no effect with ARE clear.
    control |= GICD_ARE;
    __GicWrite32(g_distributor, ARM64_GICD_CTLR, control);
    status = __WaitClear(g_distributor, ARM64_GICD_CTLR, GICD_RWP);
    if (status != OS_EOK || !(__GicRead32(g_distributor, ARM64_GICD_CTLR) & GICD_ARE)) {
        return OS_ENOTSUPPORTED;
    }

    // IDs 0-15 are SGIs and 16-31 are PPIs. With affinity routing their state
    // is kept in each redistributor, so the distributor loops start at the
    // first SPI (32). ICENABLER has one write-1-to-disable bit per ID, so each
    // write disables 32 lines without a read/modify/write.
    for (line = 32; line < g_interruptCount; line += 32) {
        offset = (line / 32) * 4;
        __GicWrite32(g_distributor, ARM64_GICD_ICENABLER + offset, 0xffffffff);
    }

    // Wait for the line disables to complete before changing the rest of the
    // configuration. Disabling Group 1 above did not clear the enable bit of
    // each line.
    status = __WaitClear(g_distributor, ARM64_GICD_CTLR, GICD_RWP);
    if (status != OS_EOK) {
        return status;
    }
    
    // Put SPIs in Group 1 for IAR1/EOIR1. These registers have one bit per ID.
    // With security enabled, firmware must have assigned our devices to
    // Group 1 Non-secure already, as EL1 cannot change secure assignments.
    // Clear stale pending and active bits too. A level-triggered device still
    // asserting its line becomes pending again, but stays disabled until a
    // driver configures it.
    for (line = 32; line < g_interruptCount; line += 32) {
        offset = (line / 32) * 4;
        __GicWrite32(g_distributor, ARM64_GICD_IGROUPR + offset, 0xffffffff);
        __GicWrite32(g_distributor, ARM64_GICD_ICPENDR + offset, 0xffffffff);
        __GicWrite32(g_distributor, GIC_ICACTIVER + offset, 0xffffffff);
    }

    // Priorities are one byte per ID, so each word sets four lines to 0xa0. A
    // lower value means a higher priority, and 0xa0 is let through by the 0xff
    // priority mask on the CPU interface. Overwrite whatever priorities firmware
    // set, so all device interrupts start out with the same priority.
    for (line = 32; line < g_interruptCount; line += 4) {
        __GicWrite32(g_distributor, ARM64_GICD_IPRIORITYR + line, ARM64_GIC_DEFAULT_PRIORITIES);
    }

    // ICFGR has two bits per ID, so 16 lines per word. Zero means
    // level-triggered. This is only the default, __Configure sets the trigger
    // mode a device needs when its line is configured.
    for (line = 32; line < g_interruptCount; line += 16) {
        __GicWrite32(g_distributor, ARM64_GICD_ICFGR + (line / 16) * 4, 0);
    }

    // Route all SPIs to the boot CPU, as the secondary CPUs have not set up their
    // CPU interfaces yet. IROUTER has one 64-bit entry per ID, hence the 8-byte
    // stride. The affinity uses the same layout as MPIDR (Aff3 at [39:32]), and
    // IRM=0 means the interrupt goes to exactly that CPU.
    for (line = 32; line < g_interruptCount; line++) {
        __GicWrite64(g_distributor, GICD_IROUTER + line * 8, affinity);
    }

    // Before the boot CPU can receive interrupts, its redistributor must be
    // woken up, its SGIs/PPIs set up and its ICC interface configured. Do that
    // before Group 1 is enabled on the distributor again. Secondary CPUs only
    // run this part when they start.
    status = __InitializeCore();
    if (status != OS_EOK) {
        return status;
    }

    // Enable Group 1 on the distributor once all earlier writes are complete.
    // The SPI lines stay disabled until drivers configure them, and IRQ masking
    // on the CPU is still up to the caller.
    GIC_BARRIER();
    __GicWrite32(g_distributor, ARM64_GICD_CTLR, control | groupEnable);
    status = __WaitClear(g_distributor, ARM64_GICD_CTLR, GICD_RWP);
    if (status != OS_EOK) {
        return status;
    }

    // Only report the number of IDs once all setup has succeeded. The common
    // interrupt code uses it to reject invalid IDs.
    *count = g_interruptCount;
    return OS_EOK;
}

static oserr_t
__Configure(
    _In_ const InterruptControllerResource_t* interrupt,
    _In_ int                                  enable)
{
    unsigned int line = interrupt->Line;
    uintptr_t    rd = CpuCorePlatformBlock(CpuCoreCurrent())->Gic.Redistributor;
    uintptr_t    bank = line < 32 ? rd + GIC_FRAME_SIZE : g_distributor;
    uintptr_t    controlBase = line < 32 ? rd : g_distributor;
    unsigned int offset = (line / 32) * 4;
    unsigned int shift = (line % 16) * 2;
    uint32_t     configuration;
    oserr_t      status;

    __GicWrite32(bank, ARM64_GICD_ICENABLER + offset, 1U << (line % 32));
    
    // A DSB does not wait for the controller. Wait for RWP, so we know the
    // disable has completed.
    status = __WaitClear(controlBase, ARM64_GICD_CTLR, line < 32 ? GICR_RWP : GICD_RWP);
    if (status != OS_EOK) {
        return status;
    }

    offset = ARM64_GICD_ICFGR + (line / 16) * 4;
    configuration = __GicRead32(bank, offset) & ~(3U << shift);
    if (!(interrupt->AcpiConform & INTERRUPT_ACPICONFORM_TRIGGERMODE)) {
        configuration |= 2U << shift;
    }

    __GicWrite32(bank, offset, configuration);
    if ((__GicRead32(bank, offset) & (3U << shift)) != (configuration & (3U << shift))) {
        return OS_ENOTSUPPORTED;
    }

    if (enable) {
        GIC_BARRIER();
        __GicWrite32(bank, ARM64_GICD_ISENABLER + (line / 32) * 4, 1U << (line % 32));
    }

    GIC_BARRIER();
    return OS_EOK;
}

static void
__EnablePrivate(
    _In_ unsigned int line)
{
    uintptr_t rd = CpuCorePlatformBlock(CpuCoreCurrent())->Gic.Redistributor;

    __GicWrite32(rd + GIC_FRAME_SIZE, ARM64_GICD_ISENABLER, 1U << line);
    GIC_BARRIER();
}

static uint32_t
__Acknowledge(void)
{
    uint64_t token;

    GIC_READ_SYSTEM(icc_iar1_el1, token);
    GIC_BARRIER();
    return (uint32_t)token;
}

static uint32_t
__Decode(
    _In_ uint32_t token)
{
    return token;
}

static void
__Complete(
    _In_ uint32_t token)
{
    // The caller has already issued a DSB for any device writes. With EOImode=0
    // this write drops the running priority and deactivates the interrupt.
    GIC_WRITE_SYSTEM(icc_eoir1_el1, token);
    GIC_BARRIER();
}

static oserr_t
__Send(
    _In_ PlatformCpuCoreBlock_t* core,
    _In_ unsigned int            line)
{
    uint64_t affinity = core->Affinity;
    uint64_t value;

    // TargetList covers the low 4 bits of Aff0, and RS holds the upper 4 bits.
    // RS is only non-zero when both the distributor and the sending CPU support
    // RSS, which is checked during setup. The caller has already issued a
    // barrier for any shared data before sending.
    value = ((affinity >> 32) & 0xff) << 48;
    value |= ((affinity >> 16) & 0xff) << 32;
    value |= ((affinity >> 8) & 0xff) << 16;
    value |= ((affinity & 0xff) >> 4) << 44;
    value |= (uint64_t)line << 24;
    value |= 1ULL << (affinity & 15);
    GIC_WRITE_SYSTEM(icc_sgi1r_el1, value);
    GIC_BARRIER();
    return OS_EOK;
}

const struct Arm64GicOperations g_arm64GicV3 = {
    .Initialize = __Initialize,
    .InitializeCore = __InitializeCore,
    .Configure = __Configure,
    .EnablePrivate = __EnablePrivate,
    .Acknowledge = __Acknowledge,
    .Decode = __Decode,
    .Complete = __Complete,
    .Send = __Send
};

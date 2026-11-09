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

#include <component/cpu.h>
#include <debug.h>
#include "private.h"
#include "device.h"
#include "gic.h"

static volatile uint32_t* g_distributor;
static volatile uint32_t* g_cpuInterface;
static unsigned int       g_interruptCount;

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

    // The first ITARGETSR word is banked per CPU, and each byte holds the target
    // mask of the CPU reading it. Some implementations leave some of the bytes
    // zero, so OR all four together before picking out a byte.
    target |= target >> 16;
    target |= target >> 8;
    return target & ARM64_GIC_BYTE_MASK;
}

static uint32_t
__GetInterruptGroup(void)
{
    uint32_t capabilities = g_distributor[ARM64_GICD_TYPER / ARM64_GIC_REGISTER_SIZE];

    // With security extensions we are in the non-secure view, and use Group 1
    // as set up by firmware. Without them, all interrupts are in Group 0.
    return (capabilities & ARM64_GIC_TYPER_SECURITY) ? ARM64_GIC_ALL_BITS : 0;
}

static oserr_t
__InitializeCore(void)
{
    PlatformCpuCoreBlock_t* core = CpuCorePlatformBlock(CpuCoreCurrent());
    unsigned int            offset;

    // Read the target bit of this CPU interface from ITARGETSR0. The MPIDR
    // affinity cannot be used as a GIC target number (they differ on BCM2712).
    core->Gic.Target = __GetCoreTarget();
    if (!core->Gic.Target) {
        return OS_ENOTSUPPORTED;
    }
    __WriteDistributor(ARM64_GICD_ICENABLER, ARM64_GIC_PPI_MASK);
    __WriteDistributor(ARM64_GICD_ICPENDR, ARM64_GIC_ALL_BITS);
    
    // Without security extensions we use Group 0 (EnableGrp0). With them, we
    // are in the non-secure view and use Group 1 as set up by firmware.
    __WriteDistributor(ARM64_GICD_IGROUPR, __GetInterruptGroup());
    for (offset = ARM64_GICD_IPRIORITYR;
         offset < ARM64_GICD_IPRIORITYR + ARM64_GIC_SPI_BASE;
         offset += ARM64_GIC_REGISTER_SIZE) {
        __WriteDistributor(offset, ARM64_GIC_DEFAULT_PRIORITIES);
    }
    __WriteDistributor(ARM64_GICD_ISENABLER, ARM64_GIC_SGI_MASK);
   
    // Let every priority through, with no subpriority split. IRQs are never
    // nested, since DAIF.I stays set while a handler runs, so handlers do not
    // need to change the priority mask.
    g_cpuInterface[ARM64_GICC_PMR / ARM64_GIC_REGISTER_SIZE] = ARM64_GIC_BYTE_MASK;
    g_cpuInterface[ARM64_GICC_BPR / ARM64_GIC_REGISTER_SIZE] = 0;
    g_cpuInterface[ARM64_GICC_CTLR / ARM64_GIC_REGISTER_SIZE] = ARM64_GIC_ENABLE;

    __asm__ volatile("dsb sy\nisb" ::: "memory");
    return OS_EOK;
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
    
    // Writing zero to the configuration bits makes every SPI level-triggered.
    for (line = ARM64_GIC_SPI_BASE; line < g_interruptCount; line += ARM64_GIC_CONFIG_PER_REGISTER) {
        offset = (line / ARM64_GIC_CONFIG_PER_REGISTER) * ARM64_GIC_REGISTER_SIZE;
        __WriteDistributor(ARM64_GICD_ICFGR + offset, 0);
    }
}

static oserr_t
__Initialize(
    _Out_ unsigned int* count)
{
    uint32_t target;
    uint32_t capabilities;
    oserr_t  status;

    // The distributor handles routing and the shared device lines, and the CPU
    // interface is where each core receives and acknowledges interrupts. Map
    // both register regions before we access the controller.
    g_distributor = (void*)Arm64MapDevice(g_arm64Platform.Gic.Distributor, g_arm64Platform.Gic.DistributorLength);
    g_cpuInterface = (void*)Arm64MapDevice(g_arm64Platform.Gic.CpuInterface, g_arm64Platform.Gic.CpuInterfaceLength);
    
    // We need both mappings to use the controller.
    if (!g_distributor || !g_cpuInterface) {
        return OS_EOOM;
    }
    
    // The configuration lock in the common code is ready at this point. Disable
    // the distributor while the boot CPU sets up the initial state.
    __WriteDistributor(ARM64_GICD_CTLR, 0);

    // TYPER reports the number of 32-interrupt blocks minus one. Convert that
    // into the number of IDs the hardware supports.
    capabilities = g_distributor[ARM64_GICD_TYPER / ARM64_GIC_REGISTER_SIZE];
    g_interruptCount = ((capabilities & ARM64_GIC_TYPER_LINES_MASK) + 1) * ARM64_GIC_BITS_PER_REGISTER;
    
    // Cap the count to stay within the kernel's interrupt tables. IDs 1020-1023
    // are special values returned on acknowledge, and cannot be registered as
    // device interrupts.
    if (g_interruptCount > ARM64_GIC_SPECIAL_ID_BASE) {
        g_interruptCount = ARM64_GIC_SPECIAL_ID_BASE;
    }

    // Route all shared device interrupts to the boot CPU to begin with. Each
    // target register holds four one-byte CPU masks, and the multiplication
    // copies the target mask of this CPU into all four bytes.
    target = __GetCoreTarget() * ARM64_GIC_REPEAT_BYTE;

    // Disable all shared lines, clear old pending requests and give each line
    // the default group, priority, target and trigger mode (level). Drivers set
    // the trigger mode they need later, when they enable a line.
    __InitializeSharedInterrupts(target);

    // Set up the private interrupts and the CPU interface of this core before
    // the distributor is enabled again. The other CPUs do their own local
    // setup when they start.
    status = __InitializeCore();
    if (status != OS_EOK) {
        return status;
    }
    __WriteDistributor(ARM64_GICD_CTLR, ARM64_GIC_ENABLE);

    DEBUG("GIC distributor=%llx cpu=%llx type=%x control=%x target=%x",
        g_arm64Platform.Gic.Distributor, g_arm64Platform.Gic.CpuInterface,
        capabilities, g_distributor[ARM64_GICD_CTLR / ARM64_GIC_REGISTER_SIZE], target);
    *count = g_interruptCount;
    return OS_EOK;
}

static oserr_t
__Configure(
    _In_ const InterruptControllerResource_t* interrupt,
    _In_ int                                  enable)
{
    unsigned int line = interrupt->Line;
    unsigned int shift;
    unsigned int offset;
    uint32_t     configuration;

    // Software interrupts are sent between CPUs and have no trigger mode. They
    // are enabled when each CPU starts, so there is nothing to do here.
    if (interrupt->Flags & INTERRUPT_SOFT) {
        return OS_EOK;
    }

    // IDs below the PPI range are SGIs, and IDs at or above the interrupt count
    // have no hardware line. Reject both before we use the line to calculate
    // register offsets.
    if (line < ARM64_GIC_PPI_BASE || line >= g_interruptCount) {
        return OS_EINVALPARAMS;
    }

    // Disable the line before changing its trigger mode. ICENABLER has one bit
    // per interrupt (32 per word). Writing a 1 disables that line and 0 bits are
    // ignored, so no read/modify/write is needed. This does not clear a pending
    // request or complete an active interrupt. The barrier in __WriteDistributor
    // makes sure the disable is done before we continue.
    offset = (line / ARM64_GIC_BITS_PER_REGISTER) * ARM64_GIC_REGISTER_SIZE;
    __WriteDistributor(ARM64_GICD_ICENABLER + offset, 1U << (line % ARM64_GIC_BITS_PER_REGISTER));

    // Only SPIs get their trigger mode changed here. PPIs are private to each
    // CPU, and keep the trigger mode set up by firmware or the platform.
    if (line >= ARM64_GIC_SPI_BASE) {
        // ICFGR has a 2-bit field per interrupt (16 per word). Find the word
        // and the position of the field for this line.
        offset = ARM64_GICD_ICFGR + (line / ARM64_GIC_CONFIG_PER_REGISTER) * ARM64_GIC_REGISTER_SIZE;
        shift = (line % ARM64_GIC_CONFIG_PER_REGISTER) * ARM64_GIC_CONFIG_BITS;

        // Unlike the set/clear enable registers, ICFGR holds the full value, so
        // keep the other 15 lines and only clear the field for this line. The
        // low bit of the field is reserved and stays zero. The high bit selects
        // edge-triggered when set and level-triggered when clear.
        configuration = g_distributor[offset / ARM64_GIC_REGISTER_SIZE] & ~(ARM64_GIC_CONFIG_MASK << shift);

        // When INTERRUPT_ACPICONFORM_TRIGGERMODE is set the interrupt is
        // level-triggered, and the device keeps its line asserted until it is
        // serviced. Otherwise the interrupt is edge-triggered.
        if (!(interrupt->AcpiConform & INTERRUPT_ACPICONFORM_TRIGGERMODE)) {
            configuration |= ARM64_GIC_CONFIG_EDGE << shift;
        }

        // Write the word back. The barrier in __WriteDistributor makes sure the
        // new setting is in place before the line is enabled.
        __WriteDistributor(offset, configuration);
    }

    // Enable the line through ISENABLER. Writing a 1 enables the line and 0 bits
    // are ignored. The offset is calculated again, as it was reused for ICFGR
    // above. If enable is not set, the line stays disabled.
    if (enable) {
        offset = (line / ARM64_GIC_BITS_PER_REGISTER) * ARM64_GIC_REGISTER_SIZE;
        __WriteDistributor(ARM64_GICD_ISENABLER + offset, 1U << (line % ARM64_GIC_BITS_PER_REGISTER));
    }

    return OS_EOK;
}

static void
__EnablePrivate(
    _In_ unsigned int line)
{
    __WriteDistributor(ARM64_GICD_ISENABLER, 1U << line);
}

static uint32_t
__Acknowledge(void)
{
    return g_cpuInterface[ARM64_GICC_IAR / ARM64_GIC_REGISTER_SIZE];
}

static uint32_t
__Decode(
    _In_ uint32_t token)
{
    // On GICv2 the token also holds the sending CPU above the 10-bit
    // interrupt ID, so mask it out.
    return token & ARM64_GIC_INTERRUPT_ID_MASK;
}

static void
__Complete(
    _In_ uint32_t token)
{
    g_cpuInterface[ARM64_GICC_EOIR / ARM64_GIC_REGISTER_SIZE] = token;
    __asm__ volatile("dsb sy" ::: "memory");
}

static oserr_t
__Send(
    _In_ PlatformCpuCoreBlock_t* core,
    _In_ unsigned int            line)
{
    __WriteDistributor(ARM64_GICD_SGIR,
        (core->Gic.Target << ARM64_GIC_SGI_TARGET_SHIFT) | line);
    return OS_EOK;
}

const struct Arm64GicOperations g_arm64GicV2 = {
    .Initialize = __Initialize,
    .InitializeCore = __InitializeCore,
    .Configure = __Configure,
    .EnablePrivate = __EnablePrivate,
    .Acknowledge = __Acknowledge,
    .Decode = __Decode,
    .Complete = __Complete,
    .Send = __Send
};

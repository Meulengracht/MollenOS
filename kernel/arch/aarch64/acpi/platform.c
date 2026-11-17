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

#define __MODULE "ACPI-ARM64"

#include <arch/acpi.h>
#include <component/cpu.h>
#include <component/ic.h>
#include <machine.h>
#include <debug.h>

#include "spcr.h"
#include "tables.h"
#include "iort.h"

static void
__PublishCores(
    _In_ const struct __AcpiPlatformDescription* description)
{
    PlatformCpuCoreBlock_t* core;
    unsigned int            logicalId = 1;

    // Logical core 0 is the CPU running this boot code, with per-CPU storage
    // and scheduler state already active. Because neither table order nor ACPI
    // IDs identify the boot CPU, BootIndex matches it by its hardware CPU ID
    // (affinity). Every other enabled CPU is registered in table order starting
    // at logical ID 1 with no gaps.
    GetMachine()->Processor.NumberOfCores = description->Count;
    
    for (unsigned int i = 0; i < description->Count; i++) {
        if (i == description->BootIndex) {
            core = CpuCorePlatformBlock(GetMachine()->Processor.Cores);
        } else {
            CpuCoreRegister(&GetMachine()->Processor, logicalId, CpuStateShutdown, 0);
            core = CpuCorePlatformBlock(GetProcessorCore(logicalId));
            logicalId++;
        }
        core->Affinity = description->Cpus[i].Affinity;
        core->Gic.RedistributorPhysical = description->Cpus[i].Redistributor;
        core->ReleaseAddress = 0;
        core->EnableMethod = ARM64_CPU_ENABLE_PSCI;
    }

    // Assume all CPUs have equal access time to all RAM for now. We do not
    // parse the SRAT table (which maps which memory sits closest to which CPU),
    // and we do not support adding or removing CPUs at runtime.
    SetMachineUmaMode();
}

oserr_t
ArchAcpiInitialize(void)
{
    struct __AcpiPlatformDescription description = {0};
    oserr_t                          status;

    // Start the serial console from the SPCR (Serial Port Console Redirection)
    // table first. It needs none of the FADT, MADT, or GTDT tables, so errors
    // from reading those later still appear on the console.
    Arm64AcpiInitializeConsole();

    status = Arm64AcpiReadFadt(&description.Platform);
    if (status != OS_EOK) {
        ERROR("ARM64 FADT discovery failed: %u", status);
        return status;
    }

    status = Arm64AcpiReadMadt(&description);
    if (status != OS_EOK) {
        ERROR("ARM64 MADT discovery failed: %u", status);
        return status;
    }

    status = Arm64AcpiReadGtdt(&description.Platform);
    if (status != OS_EOK) {
        Arm64AcpiReleaseDescription(&description, 0);
        ERROR("ARM64 GTDT discovery failed: %u", status);
        return status;
    }

    // A missing IORT is fine on platforms without an SMMU or ITS. Initialize it
    // before registering CPUs so every later DMA and interrupt lookup sees the
    // same frozen catalogue or the same error for the rest of boot.
    status = ArmIortInitialize();
    if (status != OS_EOK && status != OS_ENOENT) {
        WARNING("ARM64 IORT discovery unavailable: %u", status);
    }

    // Required tables are parsed and released now. Register the interrupt
    // controller before creating CPU records so a failure here never leaves
    // CPUs registered without an interrupt controller. CpuCoreRegister asserts
    // internally on allocation failure and halts boot rather than returning an
    // error.
    status = CreateInterruptController(
        description.GicId,
        0,
        INTERRUPT_PHYSICAL_END,
        description.Platform.Gic.Distributor
    );
    if (status != OS_EOK) {
        Arm64AcpiReleaseDescription(&description, 0);
        return status;
    }

    description.Platform.Uart = g_arm64Platform.Uart;
    description.Platform.UartLength = g_arm64Platform.UartLength;
    
    g_arm64Platform = description.Platform;
    __PublishCores(&description);
    
    Arm64AcpiReleaseDescription(&description, 1);
    DEBUG("ARM64 ACPI platform: %u CPUs, GICv%u, timer PPI %u, PSCI %u",
        description.Count, description.Platform.Gic.Version, description.Platform.TimerInterrupt, description.Platform.PsciConduit);
    return OS_EOK;
}

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

static void
__PublishCores(
    _In_ const struct __AcpiPlatformDescription* description)
{
    PlatformCpuCoreBlock_t* core;
    unsigned int            logicalId = 1;

    // Logical core 0 is the CPU we are running on, and its TLS and scheduler
    // state is already set up. MADT order and ACPI UIDs don't tell which CPU
    // booted, so it was found by its affinity (BootIndex). The other enabled
    // CPUs are registered in table order and numbered from 1 without gaps.
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

    // NUMA (SRAT) and CPU hotplug are not handled yet. For now all enabled
    // CPUs share one memory domain.
    SetMachineUmaMode();
}

oserr_t
ArchAcpiInitialize(void)
{
    struct __AcpiPlatformDescription description = {0};
    oserr_t                          status;

    // Set up the SPCR console first. It does not depend on the other tables,
    // so if FADT, MADT or GTDT is rejected below, the error still shows up on
    // the console.
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

    // All required tables have been checked and released at this point. The
    // interrupt controller is registered before any CPU records are created,
    // so a failure here can't leave us with CPUs that have no interrupt
    // controller. CpuCoreRegister can't return an error. It asserts if the
    // allocation fails, and the boot stops there.
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

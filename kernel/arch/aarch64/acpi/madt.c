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
#include <machine.h>
#include "tables.h"

/**
 * @brief Checks that an address can hold one of the fixed 4 KiB GICv2 register
 * blocks.
 *
 * The address must be nonzero, page aligned and fit in 48 bits. This catches
 * bad addresses while the MADT is read. Overlap with RAM is checked later, when
 * the registers are mapped.
 */
static int
__ValidGicAddress(
    _In_ uint64_t address)
{
    return address != 0 && address < (1ULL << 48) && !(address & (ARM64_PAGE_SIZE - 1));
}

static oserr_t
__ReadCpu(
    _In_    const ACPI_MADT_GENERIC_INTERRUPT* entry,
    _In_    uint64_t                           defaultBase,
    _InOut_ struct __AcpiPlatformDescription*   description)
{
    struct __AcpiCpuDescription* cpu;
    uint64_t                     cpuBase;
    uint64_t                     bootAffinity;

    // Skip CPUs that are disabled or only marked online-capable. Those are
    // meant for CPU hotplug, which needs its own handling, so a GICC entry
    // alone is not enough to start them. The caller has already checked the
    // entry size.
    if (!(entry->Flags & ACPI_MADT_ENABLED)) {
        return OS_EOK;
    }

    if (description->Count == ARM64_ACPI_GICV2_CPUS || entry->CpuInterfaceNumber >= ARM64_ACPI_GICV2_CPUS) {
        return OS_ENOTSUPPORTED;
    }

    if (entry->ArmMpidr & ~ARM64_MPIDR_AFFINITY_MASK) {
        return OS_EINVALPARAMS;
    }

    for (unsigned int i = 0; i < description->Count; i++) {
        cpu = &description->Cpus[i];
        if (cpu->Affinity == entry->ArmMpidr || cpu->Uid == entry->Uid ||
            cpu->InterfaceNumber == entry->CpuInterfaceNumber) {
            return OS_EINVALPARAMS;
        }
    }

    // A nonzero address in the GICC entry is used instead of the common
    // address in the MADT header. Our GIC driver uses the same banked CPU
    // interface address on every core, so all CPUs must report the same one.
    // If they don't, the MADT is rejected. Quietly using the boot CPU's
    // address for the others would be wrong, and separate addresses per CPU
    // need driver support first.
    cpuBase = entry->BaseAddress ? entry->BaseAddress : defaultBase;
    if (!__ValidGicAddress(cpuBase)) {
        return OS_EINVALPARAMS;
    }

    if (description->Count && description->Platform.CpuInterface != cpuBase) {
        return OS_ENOTSUPPORTED;
    }
    
    description->Platform.CpuInterface = cpuBase;
    description->Platform.CpuInterfaceLength = ARM64_GIC_REGION_MINIMUM;

    bootAffinity = CpuCorePlatformBlock(GetMachine()->Processor.Cores)->Affinity;
    if (entry->ArmMpidr == bootAffinity) {
        description->BootIndex = description->Count;
    }

    cpu = &description->Cpus[description->Count++];
    cpu->Affinity = entry->ArmMpidr;
    cpu->Uid = entry->Uid;
    cpu->InterfaceNumber = entry->CpuInterfaceNumber;
    return OS_EOK;
}

static oserr_t
__ReadDistributor(
    _In_    const ACPI_MADT_GENERIC_DISTRIBUTOR* entry,
    _InOut_ struct __AcpiPlatformDescription*    description)
{
    // Only GICv2 is supported. Version zero means unspecified, and we don't
    // guess the version from the address. GICv2 MMIO accesses won't work on a
    // GICv3 or v4 that uses the system register interface.
    if (entry->Version != ACPI_MADT_GIC_VERSION_V2 || description->Platform.Distributor) {
        return OS_ENOTSUPPORTED;
    }

    if (entry->GlobalIrqBase != 0 || !__ValidGicAddress(entry->BaseAddress)) {
        return OS_EINVALPARAMS;
    }

    description->GicId = entry->GicId;
    description->Platform.Distributor = entry->BaseAddress;
    description->Platform.DistributorLength = ARM64_GIC_REGION_MINIMUM;
    
    return OS_EOK;
}

static oserr_t
__ReadEntries(
    _In_    const ACPI_TABLE_MADT*            table,
    _InOut_ struct __AcpiPlatformDescription* description)
{
    const ACPI_SUBTABLE_HEADER* entry;
    const uint8_t*              current = (const uint8_t*)table + sizeof(*table);
    size_t                      remaining = table->Header.Length - sizeof(*table);
    oserr_t                     status;

    while (remaining != 0) {
        // Check the entry header and length before we cast or step past it.
        // A zero length would loop forever, and a truncated entry at the end
        // would make us read past the table.
        if (remaining < sizeof(*entry)) {
            return OS_EINVALPARAMS;
        }

        entry = (const void*)current;
        if (entry->Length < sizeof(*entry) || entry->Length > remaining) {
            return OS_EINVALPARAMS;
        }

        status = OS_EOK;
        switch (entry->Type) {
            case ACPI_MADT_TYPE_GENERIC_INTERRUPT:
                // We read up to and including MPIDR. Newer GICC revisions
                // only add fields after it.
                if (entry->Length < offsetof(ACPI_MADT_GENERIC_INTERRUPT, ArmMpidr) + sizeof(UINT64)) {
                    return OS_EINVALPARAMS;
                }
                status = __ReadCpu(
                    (const void*)entry,
                    table->Address,
                    description
                );
                break;
            case ACPI_MADT_TYPE_GENERIC_DISTRIBUTOR:
                if (entry->Length < sizeof(ACPI_MADT_GENERIC_DISTRIBUTOR)) {
                    return OS_EINVALPARAMS;
                }
                status = __ReadDistributor(
                    (const void*)entry,
                    description
                );
                break;
            default:
                // Other entries, like PMU or MSI, are not needed to start the
                // timer or the other CPUs. They are skipped, but their length
                // has still been checked above.
                break;
        }

        if (status != OS_EOK) {
            return status;
        }

        remaining -= entry->Length;
        current += entry->Length;
    }
    return OS_EOK;
}

oserr_t
Arm64AcpiReadMadt(
    _InOut_ struct __AcpiPlatformDescription* description)
{
    ACPI_TABLE_HEADER* header;
    oserr_t            status;

    status = Arm64AcpiGetTable(ACPI_SIG_MADT, sizeof(ACPI_TABLE_MADT), &header);
    if (status != OS_EOK) {
        return status;
    }

    description->BootIndex = ARM64_ACPI_GICV2_CPUS;
    
    status = __ReadEntries(
        (const ACPI_TABLE_MADT*)header,
        description
    );
    AcpiPutTable(header);
    if (status != OS_EOK) {
        return status;
    }

    if (!description->Platform.Distributor || description->BootIndex == ARM64_ACPI_GICV2_CPUS) {
        return OS_ENOENT;
    }
    
    if (description->Platform.Distributor == description->Platform.CpuInterface) {
        return OS_EINVALPARAMS;
    }
    return OS_EOK;
}

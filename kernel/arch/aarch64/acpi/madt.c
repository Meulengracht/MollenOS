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
#include <heap.h>
#include <machine.h>
#include <string.h>

#include "tables.h"

// On GICv3 the GICD and GICR frames are 64 KiB aligned. A redistributor has at
// least an RD frame and an SGI frame. Any extra frames are detected at runtime.
#define GICV3_FRAME_SIZE         0x10000ULL
#define GICV3_REDISTRIBUTOR_SIZE (2 * GICV3_FRAME_SIZE)

static int
__ValidRange(
    _In_ uint64_t base,
    _In_ uint64_t length,
    _In_ uint64_t alignment)
{
    if (!base || (base & (alignment - 1)) || !length) {
        return 0;
    }
    return base < (1ULL << 48) && length <= (1ULL << 48) - base;
}

static int
__Overlap(
    _In_ uint64_t first,
    _In_ uint64_t firstLength,
    _In_ uint64_t second,
    _In_ uint64_t secondLength)
{
    // Both ranges must have been checked with __ValidRange before this is called.
    return first < second + secondLength && second < first + firstLength;
}

static oserr_t
__ReadDistributor(
    _In_ const ACPI_MADT_GENERIC_DISTRIBUTOR* entry,
    _InOut_ struct __AcpiPlatformDescription* description)
{
    struct Arm64GicDescription* gic = &description->Platform.Gic;
    uint64_t                    length;

    if (gic->Distributor || (entry->Version != 2 && entry->Version != 3)) {
        return OS_ENOTSUPPORTED;
    }

    length = entry->Version == 3 ? GICV3_FRAME_SIZE : ARM64_GIC_REGION_MINIMUM;
    if (entry->GlobalIrqBase || !__ValidRange(entry->BaseAddress, length, length)) {
        return OS_EINVALPARAMS;
    }

    gic->Version = entry->Version;
    gic->Distributor = entry->BaseAddress;
    gic->DistributorLength = length;

    description->GicId = entry->GicId;
    return OS_EOK;
}

static oserr_t
__ReadCpu(
    _In_    const ACPI_MADT_GENERIC_INTERRUPT* entry,
    _In_    uint64_t                           defaultBase,
    _InOut_ struct __AcpiPlatformDescription*  description)
{
    struct Arm64GicDescription*  gic = &description->Platform.Gic;
    struct __AcpiCpuDescription* cpu;
    uint64_t                     base;

    if (!(entry->Flags & ACPI_MADT_ENABLED)) {
        return OS_EOK;
    }

    if (entry->ArmMpidr & ~ARM64_MPIDR_AFFINITY_MASK) {
        return OS_EINVALPARAMS;
    }

    for (unsigned int i = 0; i < description->Count; i++) {
        cpu = &description->Cpus[i];
        if (cpu->Affinity == entry->ArmMpidr || cpu->Uid == entry->Uid) {
            return OS_EINVALPARAMS;
        }
        if (gic->Version == 2 && cpu->InterfaceNumber == entry->CpuInterfaceNumber) {
            return OS_EINVALPARAMS;
        }
    }

    if (gic->Version == 2) {
        // GICC.BaseAddress takes priority over MADT.Address. The GICv2 CPU
        // interface is banked, so it must be at the same address for every CPU.
        // The target mask has 8 bits, so at most 8 CPUs are supported.
        if (description->Count == ARM64_ACPI_GICV2_CPUS ||
            entry->CpuInterfaceNumber >= ARM64_ACPI_GICV2_CPUS) {
            return OS_ENOTSUPPORTED;
        }

        base = entry->BaseAddress ? entry->BaseAddress : defaultBase;
        if (!__ValidRange(base, ARM64_GIC_REGION_MINIMUM, ARM64_PAGE_SIZE)) {
            return OS_EINVALPARAMS;
        }

        if (description->Count && gic->CpuInterface != base) {
            return OS_ENOTSUPPORTED;
        }

        if (__Overlap(base, ARM64_GIC_REGION_MINIMUM, gic->Distributor, gic->DistributorLength)) {
            return OS_EINVALPARAMS;
        }
        
        gic->CpuInterface = base;
        gic->CpuInterfaceLength = ARM64_GIC_REGION_MINIMUM;
    } else if (!gic->RegionCount) {
        // ACPI says GICR ranges take priority over the GICC addresses, so this
        // is only used when there are none. The GICC interface number and
        // GICC.BaseAddress are not used for routing on GICv3, as we go through
        // the system registers.
        base = entry->GicrBaseAddress;
        
        if (!__ValidRange(base, GICV3_REDISTRIBUTOR_SIZE, GICV3_FRAME_SIZE) ||
            __Overlap(base, GICV3_REDISTRIBUTOR_SIZE, gic->Distributor, gic->DistributorLength)) {
            return OS_EINVALPARAMS;
        }

        for (unsigned int i = 0; i < description->Count; i++) {
            if (__Overlap(base, GICV3_REDISTRIBUTOR_SIZE,
                description->Cpus[i].Redistributor, GICV3_REDISTRIBUTOR_SIZE)) {
                return OS_EINVALPARAMS;
            }
        }
    }

    if (entry->ArmMpidr == CpuCorePlatformBlock(GetMachine()->Processor.Cores)->Affinity) {
        description->BootIndex = description->Count;
    }

    cpu = &description->Cpus[description->Count++];
    cpu->Affinity = entry->ArmMpidr;
    cpu->Uid = entry->Uid;
    cpu->InterfaceNumber = entry->CpuInterfaceNumber;
    cpu->Redistributor = gic->RegionCount ? 0 : entry->GicrBaseAddress;
    return OS_EOK;
}

static oserr_t
__ReadRegion(
    _In_    const ACPI_MADT_GENERIC_REDISTRIBUTOR* entry,
    _InOut_ struct Arm64GicDescription*            gic,
    _In_    unsigned int                           index)
{
    // Non-coherent redistributors need different power management, which we do
    // not support. This version of ACPICA names the two flag bytes Reserved, so
    // fail on any non-zero value instead of ignoring a feature we lack.
    if (gic->Version != 3 || entry->Reserved) {
        return OS_ENOTSUPPORTED;
    }

    if (entry->Length < GICV3_REDISTRIBUTOR_SIZE ||
        (entry->Length & (GICV3_FRAME_SIZE - 1)) ||
        !__ValidRange(entry->BaseAddress, entry->Length, GICV3_FRAME_SIZE)) {
        return OS_EINVALPARAMS;
    }

    if (__Overlap(entry->BaseAddress, entry->Length, gic->Distributor, gic->DistributorLength)) {
        return OS_EINVALPARAMS;
    }
    
    for (unsigned int i = 0; i < index; i++) {
        if (__Overlap(entry->BaseAddress, entry->Length, gic->Regions[i].Base, gic->Regions[i].Length)) {
            return OS_EINVALPARAMS;
        }
    }

    gic->Regions[index].Base = entry->BaseAddress;
    gic->Regions[index].Length = entry->Length;
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
    unsigned int                cpuCount = 0;
    unsigned int                region = 0;
    oserr_t                     status;

    // The first pass checks the length of every entry and finds the GIC version.
    // MADT entries can come in any order, so we cannot expect GICD to come
    // before GICC. Enabled CPUs are counted here, so we know how many temporary
    // records to allocate.
    while (remaining) {
        if (remaining < sizeof(ACPI_SUBTABLE_HEADER)) {
            return OS_EINVALPARAMS;
        }

        entry = (const void*)current;
        if (entry->Length < sizeof(ACPI_SUBTABLE_HEADER) || entry->Length > remaining) {
            return OS_EINVALPARAMS;
        }
        switch (entry->Type) {
            case ACPI_MADT_TYPE_GENERIC_INTERRUPT:
                if (entry->Length < offsetof(ACPI_MADT_GENERIC_INTERRUPT, ArmMpidr) + sizeof(UINT64)) {
                    return OS_EINVALPARAMS;
                }

                if (((const ACPI_MADT_GENERIC_INTERRUPT*)entry)->Flags & ACPI_MADT_ENABLED) {
                    cpuCount++;
                }
                break;
            case ACPI_MADT_TYPE_GENERIC_DISTRIBUTOR:
                if (entry->Length < sizeof(ACPI_MADT_GENERIC_DISTRIBUTOR)) {
                    return OS_EINVALPARAMS;
                }

                status = __ReadDistributor((const void*)entry, description);
                if (status != OS_EOK) {
                    return status;
                }
                break;
            case ACPI_MADT_TYPE_GENERIC_REDISTRIBUTOR:
                if (entry->Length < sizeof(ACPI_MADT_GENERIC_REDISTRIBUTOR)) {
                    return OS_EINVALPARAMS;
                }

                description->Platform.Gic.RegionCount++;
                break;
            default:
                // ITS and other optional entries are ignored, as we do not
                // support MSI/LPI. Their length is still checked above.
                break;
        }

        current += entry->Length;
        remaining -= entry->Length;
    }

    if (!description->Platform.Gic.Distributor || !cpuCount) {
        return OS_ENOENT;
    }

    if (cpuCount > ARM64_CPU_COUNT) {
        return OS_ENOTSUPPORTED;
    }

    description->Cpus = kmalloc(cpuCount * sizeof(struct __AcpiCpuDescription));
    if (!description->Cpus) {
        return OS_EOOM;
    }

    if (description->Platform.Gic.RegionCount) {
        description->Platform.Gic.Regions = kmalloc(
            description->Platform.Gic.RegionCount * sizeof(struct Arm64GicRegion));
        if (!description->Platform.Gic.Regions) {
            return OS_EOOM;
        }
    }

    // The first pass made sure every entry fits in the table. This pass reads
    // the same entries again and does the checks for the GIC version, before
    // anything is published.
    current = (const uint8_t*)table + sizeof(*table);
    remaining = table->Header.Length - sizeof(*table);
    while (remaining) {
        entry = (const void*)current;
        status = OS_EOK;
        
        if (entry->Type == ACPI_MADT_TYPE_GENERIC_INTERRUPT) {
            status = __ReadCpu((const void*)entry, table->Address, description);
        } else if (entry->Type == ACPI_MADT_TYPE_GENERIC_REDISTRIBUTOR) {
            status = __ReadRegion((const void*)entry, &description->Platform.Gic, region++);
        }
        
        if (status != OS_EOK) {
            return status;
        }
        
        current += entry->Length;
        remaining -= entry->Length;
    }

    return description->BootIndex == ARM64_CPU_COUNT ? OS_ENOENT : OS_EOK;
}

void
Arm64AcpiReleaseDescription(
    _InOut_ struct __AcpiPlatformDescription* description,
    _In_ int published)
{
    kfree(description->Cpus);
    description->Cpus = NULL;
    if (!published) {
        kfree(description->Platform.Gic.Regions);
        description->Platform.Gic.Regions = NULL;
    }
}

oserr_t
Arm64AcpiReadMadt(
    _InOut_ struct __AcpiPlatformDescription* description)
{
    ACPI_TABLE_HEADER* header;
    oserr_t status;

    status = Arm64AcpiGetTable(ACPI_SIG_MADT, sizeof(ACPI_TABLE_MADT), &header);
    if (status != OS_EOK) {
        return status;
    }

    description->BootIndex = ARM64_CPU_COUNT;
    
    status = __ReadEntries((const ACPI_TABLE_MADT*)header, description);
    AcpiPutTable(header);
    if (status != OS_EOK) {
        Arm64AcpiReleaseDescription(description, 0);
    }
    return status;
}

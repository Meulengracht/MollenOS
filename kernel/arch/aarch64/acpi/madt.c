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

// On GICv3, the distributor (GICD) and redistributor (GICR) register blocks are
// aligned to 64 KiB. Each CPU's redistributor has at least two 64 KiB frames:
// one for control and LPI settings (RD), and one for private interrupts (SGI
// and PPI). Any extra frames are detected from hardware registers at runtime.
#define GICV3_FRAME_SIZE         0x10000ULL
#define GICV3_REDISTRIBUTOR_SIZE (2 * GICV3_FRAME_SIZE)

// A GICv3 ITS (Interrupt Translation Service) uses two back-to-back 64 KiB
// register blocks: control first, then translation. The MADT gives only the
// starting address, which must be 64 KiB aligned.
#define GICV3_ITS_SIZE           (2 * GICV3_FRAME_SIZE)

static int
__ValidRange(
    _In_ uint64_t base,
    _In_ uint64_t length,
    _In_ uint64_t alignment)
{
    if (!base || (base & (alignment - 1)) || !length) {
        return 0;
    }
    // Check that the range fits inside the 48-bit physical address space
    // without wrapping around.
    return base < (1ULL << 48) && length <= (1ULL << 48) - base;
}

static int
__Overlap(
    _In_ uint64_t first,
    _In_ uint64_t firstLength,
    _In_ uint64_t second,
    _In_ uint64_t secondLength)
{
    // Callers must check both ranges with __ValidRange first so sums cannot wrap.
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

// Checks that no earlier enabled CPU has the same hardware CPU ID (affinity) or
// ACPI processor UID. On GICv2 the target bit index must be unique as well.
static oserr_t
__CheckCpuIsUnique(
    _In_ const ACPI_MADT_GENERIC_INTERRUPT*      entry,
    _In_ const struct __AcpiPlatformDescription* description)
{
    const struct Arm64GicDescription* gic = &description->Platform.Gic;

    for (unsigned int i = 0; i < description->Count; i++) {
        const struct __AcpiCpuDescription* cpu = &description->Cpus[i];

        if (cpu->Affinity == entry->ArmMpidr || cpu->Uid == entry->Uid) {
            return OS_EINVALPARAMS;
        }
        if (gic->Version == 2 && cpu->InterfaceNumber == entry->CpuInterfaceNumber) {
            return OS_EINVALPARAMS;
        }
    }
    return OS_EOK;
}

// Reads the GICv2 CPU interface address of a CPU. Every CPU must report the
// same address, which is saved once in the GIC description.
static oserr_t
__ReadCpuInterfaceV2(
    _In_    const ACPI_MADT_GENERIC_INTERRUPT* entry,
    _In_    uint64_t                           defaultBase,
    _InOut_ struct __AcpiPlatformDescription*  description)
{
    struct Arm64GicDescription* gic = &description->Platform.Gic;
    uint64_t                    base;

    // A per-CPU GICC base address overrides the table's default address.
    // On GICv2, each CPU sees its own private register view at that same
    // physical address, so every CPU must use the same base. GICv2 targets
    // CPUs with an 8-bit mask, capping support at 8 CPUs.
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
    return OS_EOK;
}

// Checks the per-CPU GICv3 redistributor address of a CPU. It must be valid and
// must not overlap the distributor or the redistributor of an earlier CPU.
static oserr_t
__CheckRedistributorV3(
    _In_ const ACPI_MADT_GENERIC_INTERRUPT*      entry,
    _In_ const struct __AcpiPlatformDescription* description)
{
    const struct Arm64GicDescription* gic = &description->Platform.Gic;
    uint64_t                          base = entry->GicrBaseAddress;

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
    oserr_t                      status;

    if (!(entry->Flags & ACPI_MADT_ENABLED)) {
        return OS_EOK;
    }

    if (entry->ArmMpidr & ~ARM64_MPIDR_AFFINITY_MASK) {
        return OS_EINVALPARAMS;
    }

    status = __CheckCpuIsUnique(entry, description);
    if (status != OS_EOK) {
        return status;
    }

    if (gic->Version == 2) {
        status = __ReadCpuInterfaceV2(entry, defaultBase, description);
        if (status != OS_EOK) {
            return status;
        }
    } else if (!gic->RegionCount) {
        // Explicit GICR region entries take precedence over per-CPU GICC
        // redistributor addresses, so skip this when RegionCount is non-zero.
        // On GICv3, CPUs reach their interrupt interface through CPU system
        // registers, so the GICC interface number and GICC base address are not
        // needed for routing.
        status = __CheckRedistributorV3(entry, description);
        if (status != OS_EOK) {
            return status;
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
    // Non-coherent redistributors need extra cache maintenance during power
    // management, which we do not support. This ACPICA version calls the two
    // flag bytes Reserved, so fail on any non-zero value because it signals a
    // feature we cannot handle.
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
__ReadIts(
    _In_    const ACPI_MADT_GENERIC_TRANSLATOR* entry,
    _InOut_ struct Arm64GicDescription*         gic,
    _In_    unsigned int                        index)
{
    if (gic->Version != 3) {
        return OS_ENOTSUPPORTED;
    }

    if (entry->Reserved || entry->Reserved2 ||
        !__ValidRange(entry->BaseAddress, GICV3_ITS_SIZE, GICV3_FRAME_SIZE) ||
        __Overlap(entry->BaseAddress, GICV3_ITS_SIZE, gic->Distributor, gic->DistributorLength)) {
        return OS_EINVALPARAMS;
    }

    for (unsigned int i = 0; i < index; i++) {
        const Arm64ItsDescription_t* other = &gic->ItsControllers[i];

        if (entry->TranslationId == other->Identifier ||
            __Overlap(entry->BaseAddress, GICV3_ITS_SIZE, other->Base, other->Length)) {
            return OS_EINVALPARAMS;
        }
    }

    gic->ItsControllers[index].Identifier = entry->TranslationId;
    gic->ItsControllers[index].Base = entry->BaseAddress;
    gic->ItsControllers[index].Length = GICV3_ITS_SIZE;
    return OS_EOK;
}

static oserr_t
__CheckItsRedistributors(
    _In_ const struct __AcpiPlatformDescription* description)
{
    const struct Arm64GicDescription* gic = &description->Platform.Gic;

    // Check after all entries are loaded, since CPU and ITS entries can appear
    // in any order. Separate GICR regions override per-CPU GICC redistributor
    // addresses, so we need the final set of redistributors before checking for
    // overlapping memory ranges.
    for (unsigned int i = 0; i < gic->ItsCount; i++) {
        const Arm64ItsDescription_t* its = &gic->ItsControllers[i];
        unsigned int                count = gic->RegionCount ? gic->RegionCount : description->Count;

        for (unsigned int j = 0; j < count; j++) {
            uint64_t base = gic->RegionCount ? gic->Regions[j].Base : description->Cpus[j].Redistributor;
            uint64_t length = gic->RegionCount ? gic->Regions[j].Length : GICV3_REDISTRIBUTOR_SIZE;

            if (__Overlap(its->Base, its->Length, base, length)) {
                return OS_EINVALPARAMS;
            }
        }
    }
    return OS_EOK;
}

// Pass 1 helper. Checks the length of one entry and records what later passes
// need: the GIC version and distributor, the number of enabled CPUs, and the
// number of redistributor and ITS entries to allocate room for.
static oserr_t
__CheckEntry(
    _In_    const ACPI_SUBTABLE_HEADER*       entry,
    _InOut_ struct __AcpiPlatformDescription* description,
    _InOut_ unsigned int*                     cpuCount)
{
    oserr_t status;

    switch (entry->Type) {
        case ACPI_MADT_TYPE_GENERIC_INTERRUPT:
            if (entry->Length < offsetof(ACPI_MADT_GENERIC_INTERRUPT, ArmMpidr) + sizeof(UINT64)) {
                return OS_EINVALPARAMS;
            }

            if (((const ACPI_MADT_GENERIC_INTERRUPT*)entry)->Flags & ACPI_MADT_ENABLED) {
                (*cpuCount)++;
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
        case ACPI_MADT_TYPE_GENERIC_TRANSLATOR:
            if (entry->Length < sizeof(ACPI_MADT_GENERIC_TRANSLATOR)) {
                return OS_EINVALPARAMS;
            }
            if (description->Platform.Gic.ItsCount == ARM64_ACPI_MAX_ITS) {
                return OS_ENOTSUPPORTED;
            }
            description->Platform.Gic.ItsCount++;
            break;
        default:
            // Skip entry types we don't use after checking their lengths.
            break;
    }
    return OS_EOK;
}

// Pass 1. Validates every entry length, counts enabled CPUs for temporary
// allocation, and picks up the GIC version. MADT subtables can appear in
// any order, so the distributor entry (GICD) may show up after CPU entries.
static oserr_t
__ScanEntries(
    _In_    const ACPI_TABLE_MADT*            table,
    _InOut_ struct __AcpiPlatformDescription* description,
    _Out_   unsigned int*                     cpuCountOut)
{
    const uint8_t* current = (const uint8_t*)table + sizeof(*table);
    size_t         remaining = table->Header.Length - sizeof(*table);
    unsigned int   cpuCount = 0;
    oserr_t        status;

    while (remaining) {
        const ACPI_SUBTABLE_HEADER* entry = (const void*)current;

        if (remaining < sizeof(ACPI_SUBTABLE_HEADER)) {
            return OS_EINVALPARAMS;
        }

        if (entry->Length < sizeof(ACPI_SUBTABLE_HEADER) || entry->Length > remaining) {
            return OS_EINVALPARAMS;
        }

        status = __CheckEntry(entry, description, &cpuCount);
        if (status != OS_EOK) {
            return status;
        }

        current += entry->Length;
        remaining -= entry->Length;
    }

    *cpuCountOut = cpuCount;
    return OS_EOK;
}

// Allocates the arrays that pass 2 fills in, sized by the counts from pass 1.
static oserr_t
__AllocateRecords(
    _InOut_ struct __AcpiPlatformDescription* description,
    _In_    unsigned int                      cpuCount)
{
    struct Arm64GicDescription* gic = &description->Platform.Gic;

    if (!gic->Distributor || !cpuCount) {
        return OS_ENOENT;
    }

    if (cpuCount > ARM64_CPU_COUNT) {
        return OS_ENOTSUPPORTED;
    }

    description->Cpus = kmalloc(cpuCount * sizeof(struct __AcpiCpuDescription));
    if (!description->Cpus) {
        return OS_EOOM;
    }

    if (gic->RegionCount) {
        gic->Regions = kmalloc(gic->RegionCount * sizeof(struct Arm64GicRegion));
        if (!gic->Regions) {
            return OS_EOOM;
        }
    }

    if (gic->ItsCount) {
        gic->ItsControllers = kmalloc(gic->ItsCount * sizeof(Arm64ItsDescription_t));
        if (!gic->ItsControllers) {
            return OS_EOOM;
        }
    }
    return OS_EOK;
}

// Pass 2. Pass 1 already confirmed every entry fits inside the table. This
// pass parses each entry and validates GIC-version rules before anything is
// published.
static oserr_t
__ParseEntries(
    _In_    const ACPI_TABLE_MADT*            table,
    _InOut_ struct __AcpiPlatformDescription* description)
{
    const uint8_t* current = (const uint8_t*)table + sizeof(*table);
    size_t         remaining = table->Header.Length - sizeof(*table);
    unsigned int   region = 0;
    unsigned int   its = 0;
    oserr_t        status;

    while (remaining) {
        const ACPI_SUBTABLE_HEADER* entry = (const void*)current;

        status = OS_EOK;
        if (entry->Type == ACPI_MADT_TYPE_GENERIC_INTERRUPT) {
            status = __ReadCpu((const void*)entry, table->Address, description);
        } else if (entry->Type == ACPI_MADT_TYPE_GENERIC_REDISTRIBUTOR) {
            status = __ReadRegion((const void*)entry, &description->Platform.Gic, region++);
        } else if (entry->Type == ACPI_MADT_TYPE_GENERIC_TRANSLATOR) {
            status = __ReadIts((const void*)entry, &description->Platform.Gic, its++);
        }
        
        if (status != OS_EOK) {
            return status;
        }
        
        current += entry->Length;
        remaining -= entry->Length;
    }
    return OS_EOK;
}

static oserr_t
__ReadEntries(
    _In_    const ACPI_TABLE_MADT*            table,
    _InOut_ struct __AcpiPlatformDescription* description)
{
    unsigned int cpuCount;
    oserr_t      status;

    status = __ScanEntries(table, description, &cpuCount);
    if (status != OS_EOK) {
        return status;
    }

    status = __AllocateRecords(description, cpuCount);
    if (status != OS_EOK) {
        return status;
    }

    status = __ParseEntries(table, description);
    if (status != OS_EOK) {
        return status;
    }

    if (description->BootIndex == ARM64_CPU_COUNT) {
        return OS_ENOENT;
    }
    return __CheckItsRedistributors(description);
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
        kfree(description->Platform.Gic.ItsControllers);
        description->Platform.Gic.ItsControllers = NULL;
        description->Platform.Gic.ItsCount = 0;
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

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

// acpiinterface.h must come first, so ACPICA sets up its platform definitions
// before the DDK table headers are included.
#include <acpiinterface.h>
#include <ddk/pci/mcfg.h>
#include <arch/io.h>
#include <debug.h>
#include <memoryspace.h>

#include "pci.h"

#if defined(__aarch64__)
#include "../arch/aarch64/common/device.h"
#endif

// The MCFG table is never released once it is set. It is only set after the
// whole table has been validated, and before any AML can do PCI accesses.
static ACPI_TABLE_HEADER* g_pciMcfg;
static int                g_pciMcfgInvalid;

void
AcpiPciInitializeConfig(void)
{
    ACPI_TABLE_HEADER* table;
    ACPI_STATUS        status;

    status = AcpiGetTable(ACPI_SIG_MCFG, 0, &table);
    if (status == AE_NOT_FOUND) {
        return;
    }

    if (ACPI_FAILURE(status)) {
        g_pciMcfgInvalid = 1;
        return;
    }

    if (__PciValidateMcfg(table, table->Length) != OS_EOK) {
        ERROR("ACPI PCI: invalid MCFG; configuration access disabled");
        g_pciMcfgInvalid = 1;
        AcpiPutTable(table);
        return;
    }

    g_pciMcfg = table;
}

static uintptr_t
__MapConfigFunction(
    _In_ uint64_t physical)
{
#if defined(__aarch64__)
    // Map as Device-nGnRnE through Arm64MapDevice, which also checks that the
    // range is not mapped as Normal RAM. We cannot use the ACPICA table mapper
    // here, as it does not map MMIO.
    return Arm64MapDevice(physical, 4096);
#else
    uintptr_t mapping;
    oserr_t   status;

    if (physical > UINTPTR_MAX - 4095) {
        return 0;
    }
    status = MemorySpaceMap(
        GetDomainMemorySpace(),
        &(struct MemorySpaceMapOptions) {
            .PhysicalStart = (uintptr_t)physical,
            .Length = 4096,
            .Flags = MAPPING_COMMIT | MAPPING_NOCACHE | MAPPING_DEVICE | MAPPING_PERSISTENT,
            .PlacementFlags = MAPPING_PHYSICAL_CONTIGUOUS | MAPPING_VIRTUAL_GLOBAL
        },
        &mapping
    );
    return status == OS_EOK ? mapping : 0;
#endif
}

static void
__ConfigBarrier(void)
{
#if defined(__aarch64__)
    // Keep configuration accesses in order with other device register accesses,
    // and make sure writes are done before the temporary mapping is removed.
    __asm__ volatile("dsb sy" ::: "memory");
#else
    __asm__ volatile("" ::: "memory");
#endif
}

ACPI_STATUS
AcpiPciConfig(
    _In_    ACPI_PCI_ID* id,
    _In_    UINT32       reg,
    _InOut_ UINT64*      value,
    _In_    UINT32       width,
    _In_    int          write)
{
    const ACPI_MCFG_ALLOCATION* entry = NULL;
    uint64_t                    physical;
    uintptr_t                   mapping;
    uintptr_t                   address;
    size_t                      bytes;

#if defined(__i386__) || defined(__amd64__)
    size_t                      legacyValue;
    oserr_t                     status;
#endif

    // PCI configuration accesses are 8, 16 or 32 bits. ACPICA splits wider
    // fields for us, and any other width could cross into the next register
    // or function without anyone noticing.
    if (width != 8 && width != 16 && width != 32) {
        return AE_BAD_PARAMETER;
    }

    bytes = width / 8;
    if (id->Bus > 255 || id->Device > 31 || id->Function > 7) {
        return AE_BAD_PARAMETER;
    }

    if (reg >= 4096 || bytes > 4096 - reg || (reg & (bytes - 1))) {
        return AE_BAD_PARAMETER;
    }

    if (g_pciMcfgInvalid) {
        return AE_BAD_DATA;
    }
    
    if (g_pciMcfg != NULL) {
        entry = __PciFindMcfg(g_pciMcfg, id->Segment, id->Bus, id->Bus);
    }

    if (entry == NULL) {
#if defined(__i386__) || defined(__amd64__)
        // The legacy configuration ports can only reach segment 0 and the first
        // 256 bytes. Accesses to other segments or extended registers must never
        // be sent through them.
        if (id->Segment == 0 && reg + bytes <= 256) {
            if (write) {
                status = WriteDirectPci(id->Bus, id->Device, id->Function, reg, bytes, (size_t)*value);
            } else {
                status = ReadDirectPci(id->Bus, id->Device, id->Function, reg, bytes, &legacyValue);
                if (status == OS_EOK) {
                    *value = legacyValue;
                }
            }
            return status == OS_EOK ? AE_OK : AE_ERROR;
        }
#endif
        return AE_NOT_EXIST;
    }

    physical = entry->Address + ((uint64_t)id->Bus << 20) +
        ((uint64_t)id->Device << 15) + ((uint64_t)id->Function << 12);
    mapping = __MapConfigFunction(physical);
    if (!mapping) {
        return AE_NO_MEMORY;
    }
    
    address = mapping + reg;
    __ConfigBarrier();

    if (write) {
        if (bytes == 1) {
            *(volatile uint8_t*)address = (uint8_t)*value;
        } else if (bytes == 2) {
            *(volatile uint16_t*)address = (uint16_t)*value;
        } else {
            *(volatile uint32_t*)address = (uint32_t)*value;
        }
    } else {
        if (bytes == 1) {
            *value = *(volatile uint8_t*)address;
        } else if (bytes == 2) {
            *value = *(volatile uint16_t*)address;
        } else {
            *value = *(volatile uint32_t*)address;
        }
    }

    __ConfigBarrier();
    
    // The mapping is persistent, so unmapping only removes the virtual mapping
    // and never frees the configuration pages described by firmware.
    if (MemorySpaceUnmap(GetDomainMemorySpace(), mapping, 4096) != OS_EOK) {
        return AE_ERROR;
    }
    return AE_OK;
}

/**
 * MollenOS
 *
 * Copyright 2017, Philip Meulengracht
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
 *
 * ACPI(CA) Table Enumeration Interface
 */

#define __MODULE "ACPI"
#define __TRACE

#include <component/domain.h>
#include <component/cpu.h>
#include <acpiinterface.h>
#include <machine.h>
#include <heap.h>
#include <debug.h>
#include <assert.h>

// Static storage for acpi states
static int g_acpiStatus       = ACPI_NOT_AVAILABLE;
AcpiEcdt_t EmbeddedController = { 0 };

static oserr_t
__RegisterDomainCore(
        _In_ SystemDomain_t* Domain,
        _In_ uuid_t          CoreId,
        _In_ int             Override)
{
    TRACE("__RegisterDomainCore()");
    if (CoreId != CpuCoreId(Domain->CoreGroup.Cores)) {
        //RegisterApplicationCore(Domain, CoreId, CpuStateShutdown, 0);
    }
    else {

    }
    return OS_EOK;
}

/**
 * @brief Uses the SRAT table to get the number of domains present in the system.
 *
 * @param SratTableStart
 * @param SratTableEnd
 * @return
 */
static int
__GetSystemDomainCountFromSRAT(
    _In_ void*              SratTableStart,
    _In_ void*              SratTableEnd)
{
    ACPI_SUBTABLE_HEADER* sratEntry;
    int                   highestDomainId = -1;

    for (sratEntry = (ACPI_SUBTABLE_HEADER*)SratTableStart; (void *)sratEntry < SratTableEnd;) {
        // Avoid an infinite loop if we hit a bogus entry.
        if (sratEntry->Length < sizeof(ACPI_SUBTABLE_HEADER)) {
            break;
        }

        switch (sratEntry->Type) {
            case ACPI_SRAT_TYPE_CPU_AFFINITY: {
                ACPI_SRAT_CPU_AFFINITY *CpuAffinity = (ACPI_SRAT_CPU_AFFINITY*)sratEntry;
                if (CpuAffinity->Flags & ACPI_SRAT_CPU_USE_AFFINITY) {
                    // Calculate the 32 bit domain
                    uint32_t Domain = (uint32_t)CpuAffinity->ProximityDomainHi[2] << 24;
                    Domain             |= (uint32_t)CpuAffinity->ProximityDomainHi[1] << 16;
                    Domain             |= (uint32_t)CpuAffinity->ProximityDomainHi[0] << 8;
                    Domain            |= CpuAffinity->ProximityDomainLo;
                    if ((int)Domain > highestDomainId) {
                        highestDomainId = (int)Domain;
                    }
                }
            } break;

            default: {
            } break;
        }
        sratEntry = (ACPI_SUBTABLE_HEADER*)
            ACPI_ADD_PTR(ACPI_SUBTABLE_HEADER, sratEntry, sratEntry->Length);
    }
    return highestDomainId + 1;
}

/**
 * @brief Uses the SRAT table to retrieve information about the given domain id.
 *
 * @param SratTableStart
 * @param SratTableEnd
 * @param DomainId
 * @param NumberOfCores
 * @param MemoryRangeStart
 * @param MemoryRangeLength
 */
static void
__GetSystemDomainMetricsFromSRAT(
    _In_  void*             SratTableStart,
    _In_  void*             SratTableEnd,
    _In_  uint32_t          DomainId,
    _Out_ int*              NumberOfCores,
    _Out_ uintptr_t*        MemoryRangeStart,
    _Out_ size_t*           MemoryRangeLength)
{
    ACPI_SUBTABLE_HEADER* sratEntry;
    TRACE("__GetSystemDomainMetricsFromSRAT()");

    for (sratEntry = (ACPI_SUBTABLE_HEADER*)SratTableStart; (void *)sratEntry < SratTableEnd;) {
        // Avoid an infinite loop if we hit a bogus entry.
        if (sratEntry->Length < sizeof(ACPI_SUBTABLE_HEADER)) {
            return;
        }

        switch (sratEntry->Type) {
            case ACPI_SRAT_TYPE_CPU_AFFINITY: {
                ACPI_SRAT_CPU_AFFINITY *CpuAffinity = (ACPI_SRAT_CPU_AFFINITY*)sratEntry;
                if (CpuAffinity->Flags & ACPI_SRAT_CPU_USE_AFFINITY) {
                    // Calculate the 32 bit domain
                    uint32_t Domain = (uint32_t)CpuAffinity->ProximityDomainHi[2] << 24;
                    Domain             |= (uint32_t)CpuAffinity->ProximityDomainHi[1] << 16;
                    Domain             |= (uint32_t)CpuAffinity->ProximityDomainHi[0] << 8;
                    Domain            |= CpuAffinity->ProximityDomainLo;
                    TRACE("Cpu %" PRIuIN " => Domain %" PRIuIN "", CpuAffinity->ApicId, Domain);
                }
            } break;

            case ACPI_SRAT_TYPE_MEMORY_AFFINITY: {
                ACPI_SRAT_MEM_AFFINITY *MemoryAffinity = (ACPI_SRAT_MEM_AFFINITY*)sratEntry;
                if (MemoryAffinity->Flags & ACPI_SRAT_MEM_ENABLED) {
                    uint32_t Domain = MemoryAffinity->ProximityDomain;
                    TRACE("Memory 0x%" PRIxIN " => Domain %" PRIuIN "", LODWORD(MemoryAffinity->BaseAddress), Domain);

                    // ACPI_SRAT_MEM_HOT_PLUGGABLE
                    // ACPI_SRAT_MEM_NON_VOLATILE
                }
            } break;

            case ACPI_SRAT_TYPE_X2APIC_CPU_AFFINITY: {
                ACPI_SRAT_X2APIC_CPU_AFFINITY *CpuAffinity = (ACPI_SRAT_X2APIC_CPU_AFFINITY*)sratEntry;
                if (CpuAffinity->Flags & ACPI_SRAT_CPU_USE_AFFINITY) {
                    uint32_t Domain = CpuAffinity->ProximityDomain;
                    TRACE("Cpu %" PRIuIN " => Domain %" PRIuIN "", CpuAffinity->ApicId, Domain);
                    
                }
            } break;

            default: {
            } break;
        }

        sratEntry = (ACPI_SUBTABLE_HEADER*)
            ACPI_ADD_PTR(ACPI_SUBTABLE_HEADER, sratEntry, sratEntry->Length);
    }
}

/**
 * @brief Uses the SRAT table to register cores for the given domain. It will ignore all other
 * entries as memory has been setup for the domain at this point.
 *
 * @param SratTableStart
 * @param SratTableEnd
 * @param Domain
 */
static void
__EnumerateSystemCoresForDomainSRAT(
    _In_  void*             SratTableStart,
    _In_  void*             SratTableEnd,
    _In_ SystemDomain_t*    Domain)
{
    ACPI_SUBTABLE_HEADER* sratEntry;
    TRACE("__EnumerateSystemCoresForDomainSRAT()");

    for (sratEntry = (ACPI_SUBTABLE_HEADER*)SratTableStart; (void *)sratEntry < SratTableEnd;) {
        // Avoid an infinite loop if we hit a bogus entry.
        if (sratEntry->Length < sizeof(ACPI_SUBTABLE_HEADER)) {
            return;
        }

        switch (sratEntry->Type) {
            case ACPI_SRAT_TYPE_CPU_AFFINITY: {
                ACPI_SRAT_CPU_AFFINITY *CpuAffinity = (ACPI_SRAT_CPU_AFFINITY*)sratEntry;
                if (CpuAffinity->Flags & ACPI_SRAT_CPU_USE_AFFINITY) {
                    // Calculate the 32 bit domain
                    uint32_t DomainId = (uint32_t)CpuAffinity->ProximityDomainHi[2] << 24;
                    DomainId         |= (uint32_t)CpuAffinity->ProximityDomainHi[1] << 16;
                    DomainId         |= (uint32_t)CpuAffinity->ProximityDomainHi[0] << 8;
                    DomainId         |= CpuAffinity->ProximityDomainLo;
                    if (Domain->Id == DomainId) {
                        if (__RegisterDomainCore(Domain, CpuAffinity->ApicId, 0) != OS_EOK) {
                            ERROR("Failed to register domain core %" PRIuIN "", CpuAffinity->ApicId);
                        }
                    }
                }
            } break;

            case ACPI_SRAT_TYPE_X2APIC_CPU_AFFINITY: {
                ACPI_SRAT_X2APIC_CPU_AFFINITY *CpuAffinity = (ACPI_SRAT_X2APIC_CPU_AFFINITY*)sratEntry;
                if (CpuAffinity->Flags & ACPI_SRAT_CPU_USE_AFFINITY) {
                    uint32_t DomainId = CpuAffinity->ProximityDomain;
                    if (Domain->Id == DomainId) {
                        if (__RegisterDomainCore(Domain, CpuAffinity->ApicId, 1) != OS_EOK) {
                            ERROR("Failed to register domain core %" PRIuIN "", CpuAffinity->ApicId);
                        }
                    }
                }
            } break;

            default: {
            } break;
        }
        sratEntry = (ACPI_SUBTABLE_HEADER*)
            ACPI_ADD_PTR(ACPI_SUBTABLE_HEADER, sratEntry, sratEntry->Length);
    }
}

/**
 * @brief
 *
 * @param MadtTableStart
 * @param MadtTableEnd
 * @param RegisterCores
 * @param CoreCountOut
 */
static void
__EnumerateSystemCoresMADT(
    _In_  void* MadtTableStart,
    _In_  void* MadtTableEnd,
    _In_  int   RegisterCores,
    _Out_ int*  CoreCountOut)
{
    ACPI_SUBTABLE_HEADER* MadtEntry;
    int                   CoreCount = 0;

    for (MadtEntry = (ACPI_SUBTABLE_HEADER*)MadtTableStart; (void*)MadtEntry < MadtTableEnd;) {
        // Avoid an infinite loop if we hit a bogus entry.
        if (MadtEntry->Length < sizeof(ACPI_SUBTABLE_HEADER)) {
            break;
        }

        switch (MadtEntry->Type) {
            case ACPI_MADT_TYPE_LOCAL_APIC: {
                // Cast to correct MADT structure
                ACPI_MADT_LOCAL_APIC *AcpiCpu = (ACPI_MADT_LOCAL_APIC*)MadtEntry;
                if (AcpiCpu->LapicFlags & 0x1) {
                    CoreCount++;
                    if (RegisterCores) {
                        uint8_t ApicId      = AcpiCpu->Id;
                        uint8_t ProcessorId = AcpiCpu->ProcessorId;
                        TRACE(" > core %u (%u) available and active", ApicId, ProcessorId);
                        if (ApicId != CpuCoreId(GetMachine()->Processor.Cores)) {
                            CpuCoreRegister(&GetMachine()->Processor, ApicId, CpuStateShutdown, 0);
                        }
                    }
                }
            } break;
            case ACPI_MADT_TYPE_LOCAL_X2APIC: {
                ACPI_MADT_LOCAL_X2APIC *AcpiCpu = (ACPI_MADT_LOCAL_X2APIC*)MadtEntry;
                if (AcpiCpu->LapicFlags & 0x1) {
                    TRACE(" > core %u available for xapic2", AcpiCpu->LocalApicId);
                    TODO("missing support for X2 apics");
                }
            } break;

            default: {
            } break;
        }
        MadtEntry = (ACPI_SUBTABLE_HEADER*)
            ACPI_ADD_PTR(ACPI_SUBTABLE_HEADER, MadtEntry, MadtEntry->Length);
    }
    
    // Guard against no pointer and invalid MADT table
    if (CoreCountOut && CoreCount != 0) {
        *CoreCountOut = CoreCount;
    }
}

/**
 * @brief Retrieves the count of system interrupt overrides as that must be done before
 * enumerating them.
 *
 * @param MadtTableStart
 * @param MadtTableEnd
 * @return
 */
static int
__EnumerateSystemInterruptOverrides(
    _In_ void*              MadtTableStart,
    _In_ void*              MadtTableEnd)
{
    ACPI_SUBTABLE_HEADER* madtEntry;
    int                   overrideCount = 0;

    for (madtEntry = (ACPI_SUBTABLE_HEADER*)MadtTableStart; (void *)madtEntry < MadtTableEnd;) {
        // Avoid an infinite loop if we hit a bogus entry.
        if (madtEntry->Length < sizeof(ACPI_SUBTABLE_HEADER)) {
            break;
        }

        switch (madtEntry->Type) {
            case ACPI_MADT_TYPE_INTERRUPT_OVERRIDE: {
                overrideCount++;
            } break;

            default:
                break;
        }
        madtEntry = (ACPI_SUBTABLE_HEADER*)
            ACPI_ADD_PTR(ACPI_SUBTABLE_HEADER, madtEntry, madtEntry->Length);
    }
    return overrideCount;
}

/**
 * @brief Enumerates the MADT entries for hardware information. This relates closely
 * to the system topology and it's configuration.
 *
 * @param MadtTableStart
 * @param MadtTableEnd
 */
static void
__EnumerateSystemHardwareMADT(
    _In_ void*              MadtTableStart,
    _In_ void*              MadtTableEnd)
{
    ACPI_SUBTABLE_HEADER* madtEntry;

    for (madtEntry = (ACPI_SUBTABLE_HEADER*)MadtTableStart; (void *)madtEntry < MadtTableEnd;) {
        // Avoid an infinite loop if we hit a bogus entry.
        if (madtEntry->Length < sizeof(ACPI_SUBTABLE_HEADER)) {
            return;
        }

        switch (madtEntry->Type) {
            case ACPI_MADT_TYPE_IO_APIC: {
                ACPI_MADT_IO_APIC *IoApic = (ACPI_MADT_IO_APIC*)madtEntry;
                TRACE(" > io-apic: %" PRIuIN "", IoApic->Id);
                if (CreateInterruptController(IoApic->Id, (int)IoApic->GlobalIrqBase, 24, IoApic->Address) != OS_EOK) {
                    ERROR("Failed to register interrupt-controller");   
                }
            } break;

            case ACPI_MADT_TYPE_INTERRUPT_OVERRIDE: {
                ACPI_MADT_INTERRUPT_OVERRIDE *Override = (ACPI_MADT_INTERRUPT_OVERRIDE*)madtEntry;
                if (RegisterInterruptOverride(Override->SourceIrq, Override->GlobalIrq, Override->IntiFlags) != OS_EOK) {
                    ERROR("Failed to register interrupt-override");
                }
            } break;

            default:
                break;
        }
        madtEntry = (ACPI_SUBTABLE_HEADER*)
            ACPI_ADD_PTR(ACPI_SUBTABLE_HEADER, madtEntry, madtEntry->Length);
    }
}

/**
 * @brief We need this to fully initialize ACPI if it needs to be available.
 *
 * @param TableStart
 * @param TableEnd
 */
static void
__ParseECDT(
    _In_ void *TableStart,
    _In_ void *TableEnd)
{
    ACPI_TABLE_ECDT* ecdtTable;

    // Initialize pointers
    ecdtTable = (ACPI_TABLE_ECDT*)TableStart;

    // Store the most relevant data
    EmbeddedController.Handle = ACPI_ROOT_OBJECT;
    EmbeddedController.Gpe    = ecdtTable->Gpe;
    EmbeddedController.UId    = ecdtTable->Uid;
    memcpy(&EmbeddedController.CommandAddress, &ecdtTable->Control, sizeof(ACPI_GENERIC_ADDRESS));
    memcpy(&EmbeddedController.DataAddress, &ecdtTable->Data, sizeof(ACPI_GENERIC_ADDRESS));
    memcpy(&EmbeddedController.NsPath[0], &ecdtTable->Id[0], strlen((const char*)&(ecdtTable->Id[0])));
}

static oserr_t
__ParseMADT(
        _In_ ACPI_TABLE_HEADER* header)
{
    ACPI_TABLE_MADT* madt = (ACPI_TABLE_MADT*)header;
    void*            madtStart;
    void*            madtEnd;
    int              numberOfDomains = 0;
    TRACE("__ParseMADT()");

    madtStart = (void*)((uintptr_t)madt + sizeof(ACPI_TABLE_MADT));
    madtEnd   = (void*)((uintptr_t)madt + madt->Header.Length);

    // If the MADT table is present we can check for multiple processors
    // before doing this we check for presence of multiple domains
    if (ACPI_SUCCESS(AcpiGetTable(ACPI_SIG_SRAT, 0, &header))) {
        ACPI_TABLE_SRAT *SratTable  = (ACPI_TABLE_SRAT*)header;
        void *SratTableStart        = (void*)((uintptr_t)SratTable + sizeof(ACPI_TABLE_SRAT));
        void *SratTableEnd          = (void*)((uintptr_t)SratTable + SratTable->Header.Length);

        TRACE("__ParseMADT gathering system topology information (SRAT)");

        // Get number of domains
        numberOfDomains = __GetSystemDomainCountFromSRAT(SratTableStart, SratTableEnd);
        if (numberOfDomains > 1) {
            TRACE("__ParseMADT number of domains %" PRIiIN "", numberOfDomains);
            atomic_store(&GetMachine()->NumberOfCores, 0);

            for (int i = 0; i < numberOfDomains; i++) {
                SystemDomain_t *Domain;
                uintptr_t MemoryStart   = 0;
                size_t MemoryLength     = 0;
                int NumberOfCores       = 0;
                __GetSystemDomainMetricsFromSRAT(SratTableStart, SratTableEnd,
                                                 (uint32_t) i, &NumberOfCores, &MemoryStart, &MemoryLength);
                WARNING("end for now");
                atomic_fetch_add(&GetMachine()->NumberOfCores, NumberOfCores);
                for(;;);

                // Validate not empty domain
                if (NumberOfCores != 0) {
                    // Create the domain, then enumerate the cores for that domain
                    CreateNumaDomain((uuid_t)i, NumberOfCores, MemoryStart, MemoryLength, &Domain);
                    __EnumerateSystemCoresForDomainSRAT(SratTableStart, SratTableEnd, Domain);
                }
            }
        }

        // Cleanup
        AcpiPutTable((ACPI_TABLE_HEADER*)SratTable);
    }

    // Did we find multiple domains?
    if (numberOfDomains <= 1) {
        // No domains present, system is UMA
        // Use the MADT to enumerate system cores
        TRACE("__ParseMADT uma/acpi system, single domain");
        if (GetMachine()->Processor.NumberOfCores == 1) {
            // Check for cores anyways, some times the MADT knows things
            // that we don't. So in the first run we simply count cores and
            // update the saved value we have
            __EnumerateSystemCoresMADT(madtStart, madtEnd, 0,
                                       &GetMachine()->Processor.NumberOfCores);
        }
        __EnumerateSystemCoresMADT(madtStart, madtEnd, 1, NULL);

        // Update the total number of cores
        atomic_store(&GetMachine()->NumberOfCores, GetMachine()->Processor.NumberOfCores);
    }

    // Handle system interrupt overrides
    numberOfDomains = __EnumerateSystemInterruptOverrides(madtStart, madtEnd);
    if (numberOfDomains > 0) {
        CreateInterruptOverrides(numberOfDomains);
    }

    // Now enumerate the present hardware as now know where they go
    __EnumerateSystemHardwareMADT(madtStart, madtEnd);
    return OS_EOK;
}

oserr_t
AcpiInitializeEarly(void)
{
    ACPI_TABLE_HEADER* header;
    oserr_t         osStatus = OS_EOK;
    ACPI_STATUS        acpiStatus;
    TRACE("AcpiInitializeEarly()");

    // Perform the initial setup of ACPICA
    acpiStatus = AcpiInitializeSubsystem();
    if (ACPI_FAILURE(acpiStatus)) {
        ERROR("AcpiInitializeEarly acpi is not available (acpi disabled %" PRIuIN ")", acpiStatus);
        g_acpiStatus = ACPI_NOT_AVAILABLE;
        return OS_EUNKNOWN;
    }

    // Do the early table enumeration
    acpiStatus = AcpiInitializeTables(NULL, ACPI_MAX_INIT_TABLES, TRUE);
    if (ACPI_FAILURE(acpiStatus)) {
        ERROR("AcpiInitializeEarly failed to obtain acpi tables (acpi disabled %" PRIuIN ")", acpiStatus);
        g_acpiStatus = ACPI_NOT_AVAILABLE;
        return OS_EUNKNOWN;
    }
    else {
        g_acpiStatus = ACPI_AVAILABLE;
    }

    // Check for MADT presence and enumerate
    if (ACPI_SUCCESS(AcpiGetTable(ACPI_SIG_MADT, 0, &header))) {
        __ParseMADT(header);
        AcpiPutTable(header);
    }
    else {
        osStatus = OS_EUNKNOWN;
    }

    // Check for ECDT presence and enumerate. This table is not present on
    // any of the modern systems, they instead appear in the acpi namespace
    if (ACPI_SUCCESS(AcpiGetTable(ACPI_SIG_ECDT, 0, &header))) {
        ACPI_TABLE_ECDT* ecdtTable = (ACPI_TABLE_ECDT*)header;
        TRACE("AcpiInitializeEarly Enumerating the ECDT Table");
        TODO("missing implementation for ACPI ECDT parsing");
        //AcpiEnumerateECDT((void*)((uintptr_t)EcdtTable + sizeof(ACPI_TABLE_ECDT)),
        //    (void*)((uintptr_t)EcdtTable + EcdtTable->Header.Length));
        
        // Cleanup table when we are done with it as we are using
        // static pointers and reaollcating later
        AcpiPutTable(header);
    }

    // Check for SBST presence and enumerate
    if (ACPI_SUCCESS(AcpiGetTable(ACPI_SIG_SBST, 0, &header))) {
        ACPI_TABLE_SBST* sbspTable = (ACPI_TABLE_SBST*)header;
        TRACE("AcpiInitializeEarly Parsing the SBST Table");
        TODO("missing implementation for ACPI SBST parsing");
        
        // Cleanup table when we are done with it as we are using
        // static pointers and reaollcating later
        AcpiPutTable(header);
    }
    return osStatus;
}

int AcpiAvailable(void)
{
    return g_acpiStatus;
}

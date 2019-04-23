/* MollenOS
 *
 * Copyright 2011 - 2017, Philip Meulengracht
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
 * along with this program.If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * MollenOS MCore - ACPI(CA) Table Enumeration Interface
 */
#define __MODULE "TBIF"
#define __TRACE

#include <component/domain.h>
#include <component/cpu.h>
#include <acpiinterface.h>
#include <machine.h>
#include <heap.h>
#include <debug.h>
#include <assert.h>

// Static storage for acpi states
static int AcpiStatus           = ACPI_NOT_AVAILABLE;
AcpiEcdt_t EmbeddedController   = { 0 };

/* RegisterDomainCore
 * Registers a new core with the system for the given domain. Provides an override 
 * to support X2 entries that probably will appear. */
OsStatus_t
RegisterDomainCore(
    _In_ SystemDomain_t*    Domain,
    _In_ UUId_t             CoreId,
    _In_ int                Override)
{
    if (CoreId != Domain->CoreGroup.PrimaryCore.Id) {
        //RegisterApplicationCore(Domain, CoreId, CpuStateShutdown, 0);
    }
    else {

    }
    return OsSuccess;
}

/* GetSystemDomainCountFromSRAT
 * Uses the SRAT table to get the number of domains present in the system. */
int
GetSystemDomainCountFromSRAT(
    _In_ void*              SratTableStart,
    _In_ void*              SratTableEnd)
{
    // Variables
    ACPI_SUBTABLE_HEADER *SratEntry = NULL;
    int HighestDomainId = -1;

    for (SratEntry = (ACPI_SUBTABLE_HEADER*)SratTableStart; (void *)SratEntry < SratTableEnd;) {
        // Avoid an infinite loop if we hit a bogus entry.
        if (SratEntry->Length < sizeof(ACPI_SUBTABLE_HEADER)) {
            break;
        }

        switch (SratEntry->Type) {
            case ACPI_SRAT_TYPE_CPU_AFFINITY: {
                ACPI_SRAT_CPU_AFFINITY *CpuAffinity = (ACPI_SRAT_CPU_AFFINITY*)SratEntry;
                if (CpuAffinity->Flags & ACPI_SRAT_CPU_USE_AFFINITY) {
                    // Calculate the 32 bit domain
                    uint32_t Domain = (uint32_t)CpuAffinity->ProximityDomainHi[2] << 24;
                    Domain             |= (uint32_t)CpuAffinity->ProximityDomainHi[1] << 16;
                    Domain             |= (uint32_t)CpuAffinity->ProximityDomainHi[0] << 8;
                    Domain            |= CpuAffinity->ProximityDomainLo;
                    if ((int)Domain > HighestDomainId) {
                        HighestDomainId = (int)Domain;
                    }
                }
            } break;

            default: {
            } break;
        }
        SratEntry = (ACPI_SUBTABLE_HEADER*)
            ACPI_ADD_PTR(ACPI_SUBTABLE_HEADER, SratEntry, SratEntry->Length);
    }
    return HighestDomainId + 1;
}

/* GetSystemDomainMetricsFromSRAT
 * Uses the SRAT table to retrieve information about the given domain id. */
void
GetSystemDomainMetricsFromSRAT(
    _In_  void*             SratTableStart,
    _In_  void*             SratTableEnd,
    _In_  uint32_t          DomainId,
    _Out_ int*              NumberOfCores,
    _Out_ uintptr_t*        MemoryRangeStart,
    _Out_ size_t*           MemoryRangeLength)
{
    // Variables
    ACPI_SUBTABLE_HEADER *SratEntry = NULL;

    for (SratEntry = (ACPI_SUBTABLE_HEADER*)SratTableStart; (void *)SratEntry < SratTableEnd;) {
        // Avoid an infinite loop if we hit a bogus entry.
        if (SratEntry->Length < sizeof(ACPI_SUBTABLE_HEADER)) {
            return;
        }

        switch (SratEntry->Type) {
            case ACPI_SRAT_TYPE_CPU_AFFINITY: {
                ACPI_SRAT_CPU_AFFINITY *CpuAffinity = (ACPI_SRAT_CPU_AFFINITY*)SratEntry;
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
                ACPI_SRAT_MEM_AFFINITY *MemoryAffinity = (ACPI_SRAT_MEM_AFFINITY*)SratEntry;
                if (MemoryAffinity->Flags & ACPI_SRAT_MEM_ENABLED) {
                    uint32_t Domain = MemoryAffinity->ProximityDomain;
                    TRACE("Memory 0x%" PRIxIN " => Domain %" PRIuIN "", LODWORD(MemoryAffinity->BaseAddress), Domain);

                    // ACPI_SRAT_MEM_HOT_PLUGGABLE
                    // ACPI_SRAT_MEM_NON_VOLATILE
                }
            } break;

            case ACPI_SRAT_TYPE_X2APIC_CPU_AFFINITY: {
                ACPI_SRAT_X2APIC_CPU_AFFINITY *CpuAffinity = (ACPI_SRAT_X2APIC_CPU_AFFINITY*)SratEntry;
                if (CpuAffinity->Flags & ACPI_SRAT_CPU_USE_AFFINITY) {
                    uint32_t Domain = CpuAffinity->ProximityDomain;
                    TRACE("Cpu %" PRIuIN " => Domain %" PRIuIN "", CpuAffinity->ApicId, Domain);
                    
                }
            } break;

            default: {
            } break;
        }

        SratEntry = (ACPI_SUBTABLE_HEADER*)
            ACPI_ADD_PTR(ACPI_SUBTABLE_HEADER, SratEntry, SratEntry->Length);
    }
}

/* EnumerateSystemCoresForDomainSRAT
 * Uses the SRAT table to register cores for the given domain. It will ignore all other
 * entries as memory has been setup for the domain at this point. */
void
EnumerateSystemCoresForDomainSRAT(
    _In_  void*             SratTableStart,
    _In_  void*             SratTableEnd,
    _In_ SystemDomain_t*    Domain)
{
    // Variables
    ACPI_SUBTABLE_HEADER *SratEntry = NULL;

    for (SratEntry = (ACPI_SUBTABLE_HEADER*)SratTableStart; (void *)SratEntry < SratTableEnd;) {
        // Avoid an infinite loop if we hit a bogus entry.
        if (SratEntry->Length < sizeof(ACPI_SUBTABLE_HEADER)) {
            return;
        }

        switch (SratEntry->Type) {
            case ACPI_SRAT_TYPE_CPU_AFFINITY: {
                ACPI_SRAT_CPU_AFFINITY *CpuAffinity = (ACPI_SRAT_CPU_AFFINITY*)SratEntry;
                if (CpuAffinity->Flags & ACPI_SRAT_CPU_USE_AFFINITY) {
                    // Calculate the 32 bit domain
                    uint32_t DomainId = (uint32_t)CpuAffinity->ProximityDomainHi[2] << 24;
                    DomainId         |= (uint32_t)CpuAffinity->ProximityDomainHi[1] << 16;
                    DomainId         |= (uint32_t)CpuAffinity->ProximityDomainHi[0] << 8;
                    DomainId         |= CpuAffinity->ProximityDomainLo;
                    if (Domain->Id == DomainId) {
                        if (RegisterDomainCore(Domain, CpuAffinity->ApicId, 0) != OsSuccess) {
                            ERROR("Failed to register domain core %" PRIuIN "", CpuAffinity->ApicId);
                        }
                    }
                }
            } break;

            case ACPI_SRAT_TYPE_X2APIC_CPU_AFFINITY: {
                ACPI_SRAT_X2APIC_CPU_AFFINITY *CpuAffinity = (ACPI_SRAT_X2APIC_CPU_AFFINITY*)SratEntry;
                if (CpuAffinity->Flags & ACPI_SRAT_CPU_USE_AFFINITY) {
                    uint32_t DomainId = CpuAffinity->ProximityDomain;
                    if (Domain->Id == DomainId) {
                        if (RegisterDomainCore(Domain, CpuAffinity->ApicId, 1) != OsSuccess) {
                            ERROR("Failed to register domain core %" PRIuIN "", CpuAffinity->ApicId);
                        }
                    }
                }
            } break;

            default: {
            } break;
        }
        SratEntry = (ACPI_SUBTABLE_HEADER*)
            ACPI_ADD_PTR(ACPI_SUBTABLE_HEADER, SratEntry, SratEntry->Length);
    }
}

void
EnumerateSystemCoresMADT(
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
                        TRACE(" > core %" PRIuIN " (%" PRIuIN ") available and active", AcpiCpu->Id, AcpiCpu->ProcessorId);
                        if (AcpiCpu->Id != GetMachine()->Processor.PrimaryCore.Id) {
                            RegisterApplicationCore(&GetMachine()->Processor, AcpiCpu->Id, CpuStateShutdown, 0);
                        }
                    }
                }
            } break;
            case ACPI_MADT_TYPE_LOCAL_X2APIC: {
                ACPI_MADT_LOCAL_X2APIC *AcpiCpu = (ACPI_MADT_LOCAL_X2APIC*)MadtEntry;
                if (AcpiCpu->LapicFlags & 0x1) {
                    TRACE(" > core %" PRIuIN " available for xapic2", AcpiCpu->LocalApicId);
                    //@todo
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

/* EnumerateSystemInterruptOverrides
 * Retrieves the count of system interrupt overrides as that must be done before
 * enumerating them. */
int
EnumerateSystemInterruptOverrides(
    _In_ void*              MadtTableStart,
    _In_ void*              MadtTableEnd)
{
    // Variables
    ACPI_SUBTABLE_HEADER *MadtEntry = NULL;
    int OverrideCount = 0;

    for (MadtEntry = (ACPI_SUBTABLE_HEADER*)MadtTableStart; (void *)MadtEntry < MadtTableEnd;) {
        // Avoid an infinite loop if we hit a bogus entry.
        if (MadtEntry->Length < sizeof(ACPI_SUBTABLE_HEADER)) {
            break;
        }

        switch (MadtEntry->Type) {
            case ACPI_MADT_TYPE_INTERRUPT_OVERRIDE: {
                OverrideCount++;
            } break;

            default:
                break;
        }
        MadtEntry = (ACPI_SUBTABLE_HEADER*)
            ACPI_ADD_PTR(ACPI_SUBTABLE_HEADER, MadtEntry, MadtEntry->Length);
    }
    return OverrideCount;
}

/* EnumerateSystemHardwareMADT
 * Enumerates the MADT entries for hardware information. This relates closely
 * to the system topology and it's configuration. */
void
EnumerateSystemHardwareMADT(
    _In_ void*              MadtTableStart,
    _In_ void*              MadtTableEnd)
{
    // Variables
    ACPI_SUBTABLE_HEADER *MadtEntry = NULL;

    for (MadtEntry = (ACPI_SUBTABLE_HEADER*)MadtTableStart; (void *)MadtEntry < MadtTableEnd;) {
        // Avoid an infinite loop if we hit a bogus entry.
        if (MadtEntry->Length < sizeof(ACPI_SUBTABLE_HEADER)) {
            return;
        }

        switch (MadtEntry->Type) {
            case ACPI_MADT_TYPE_IO_APIC: {
                ACPI_MADT_IO_APIC *IoApic = (ACPI_MADT_IO_APIC*)MadtEntry;
                TRACE(" > io-apic: %" PRIuIN "", IoApic->Id);
                if (CreateInterruptController(IoApic->Id, (int)IoApic->GlobalIrqBase, 24, IoApic->Address) != OsSuccess) {
                    ERROR("Failed to register interrupt-controller");   
                }
            } break;

            case ACPI_MADT_TYPE_INTERRUPT_OVERRIDE: {
                ACPI_MADT_INTERRUPT_OVERRIDE *Override = (ACPI_MADT_INTERRUPT_OVERRIDE*)MadtEntry;
                if (RegisterInterruptOverride(Override->SourceIrq, Override->GlobalIrq, Override->IntiFlags) != OsSuccess) {
                    ERROR("Failed to register interrupt-override");
                }
            } break;

            default:
                break;
        }
        MadtEntry = (ACPI_SUBTABLE_HEADER*)
            ACPI_ADD_PTR(ACPI_SUBTABLE_HEADER, MadtEntry, MadtEntry->Length);
    }
}

/* Enumerate the ECDT 
 * We need this to fully initialize ACPI if it needs to be available. */
void
AcpiEnumerateECDT(
    _In_ void *TableStart,
    _In_ void *TableEnd)
{
    // Variables
    ACPI_TABLE_ECDT *EcdtTable = NULL;

    // Initialize pointers
    EcdtTable = (ACPI_TABLE_ECDT*)TableStart;

    // Store the most relevant data
    EmbeddedController.Handle   = ACPI_ROOT_OBJECT;
    EmbeddedController.Gpe      = EcdtTable->Gpe;
    EmbeddedController.UId      = EcdtTable->Uid;
    memcpy(&EmbeddedController.CommandAddress,  &EcdtTable->Control,    sizeof(ACPI_GENERIC_ADDRESS));
    memcpy(&EmbeddedController.DataAddress,     &EcdtTable->Data,       sizeof(ACPI_GENERIC_ADDRESS));
    memcpy(&EmbeddedController.NsPath[0],       &EcdtTable->Id[0],      strlen((const char*)&EcdtTable->Id[0]));
}

/* AcpiInitializeEarly
 * Initializes Early Access and enumerates the APIC Table */
OsStatus_t
AcpiInitializeEarly(void)
{
    // Variables
    ACPI_TABLE_HEADER *Header   = NULL;
    OsStatus_t Result           = OsSuccess;
    ACPI_STATUS Status;

    // Perform the initial setup of ACPICA
    Status = AcpiInitializeSubsystem();
    if (ACPI_FAILURE(Status)) {
        ERROR(" > acpi is not available (acpi disabled %" PRIuIN ")", Status);
        AcpiStatus = ACPI_NOT_AVAILABLE;
        return OsError;
    }

    // Do the early table enumeration
    Status = AcpiInitializeTables(NULL, ACPI_MAX_INIT_TABLES, TRUE);
    if (ACPI_FAILURE(Status)) {
        ERROR(" > failed to obtain acpi tables (acpi disabled %" PRIuIN ")", Status);
        AcpiStatus = ACPI_NOT_AVAILABLE;
        return OsError;
    }
    else {
        AcpiStatus = ACPI_AVAILABLE;
    }

    // Check for MADT presence and enumerate
    if (ACPI_SUCCESS(AcpiGetTable(ACPI_SIG_MADT, 0, &Header))) {
        ACPI_TABLE_MADT *MadtTable  = (ACPI_TABLE_MADT*)Header;
        void *MadtTableStart        = (void*)((uintptr_t)MadtTable + sizeof(ACPI_TABLE_MADT));
        void *MadtTableEnd          = (void*)((uintptr_t)MadtTable + MadtTable->Header.Length);
        int NumberOfDomains         = 0;
        TRACE(" > gathering system hardware information (MADT)");

        // If the MADT table is present we can check for multiple processors
        // before doing this we check for presence of multiple domains
        if (ACPI_SUCCESS(AcpiGetTable(ACPI_SIG_SRAT, 0, &Header))) {
            ACPI_TABLE_SRAT *SratTable  = (ACPI_TABLE_SRAT*)Header;
            void *SratTableStart        = (void*)((uintptr_t)SratTable + sizeof(ACPI_TABLE_SRAT));
            void *SratTableEnd          = (void*)((uintptr_t)SratTable + SratTable->Header.Length);
            
            TRACE(" > gathering system topology information (SRAT)");

            // Get number of domains
            NumberOfDomains = GetSystemDomainCountFromSRAT(SratTableStart, SratTableEnd);
            if (NumberOfDomains > 1) {
                TRACE(" > number of domains %" PRIiIN "", NumberOfDomains);
                for (int i = 0; i < NumberOfDomains; i++) {
                    SystemDomain_t *Domain;
                    uintptr_t MemoryStart   = 0;
                    size_t MemoryLength     = 0;
                    int NumberOfCores       = 0;
                    GetSystemDomainMetricsFromSRAT(SratTableStart, SratTableEnd,
                        (uint32_t)i, &NumberOfCores, &MemoryStart, &MemoryLength);
                    WARNING("end for now");
                    for(;;);

                    // Validate not empty domain
                    if (NumberOfCores != 0) {
                        // Create the domain, then enumerate the cores for that domain
                        CreateNumaDomain((UUId_t)i, NumberOfCores, MemoryStart, MemoryLength, &Domain);
                        EnumerateSystemCoresForDomainSRAT(SratTableStart, SratTableEnd, Domain);
                    }
                }
            }

            // Cleanup
            AcpiPutTable((ACPI_TABLE_HEADER*)SratTable);
        }

        // Did we find multiple domains?
        if (NumberOfDomains <= 1) {
            // No domains present, system is UMA
            // Use the MADT to enumerate system cores
            TRACE(" > uma/acpi system, single domain");
            if (GetMachine()->Processor.NumberOfCores == 1) {
                // Check for cores anyways, some times the MADT knows things
                // that we don't. So in the first run we simply count cores and
                // update the saved value we have
                EnumerateSystemCoresMADT(MadtTableStart, MadtTableEnd, 0, 
                    &GetMachine()->Processor.NumberOfCores);
            }
            EnumerateSystemCoresMADT(MadtTableStart, MadtTableEnd, 1, NULL);
        }

        // Handle system interrupt overrides
        NumberOfDomains = EnumerateSystemInterruptOverrides(MadtTableStart, MadtTableEnd);
        if (NumberOfDomains > 0) {
            CreateInterruptOverrides(NumberOfDomains);
        }

        // Now enumerate the present hardware as now know where they go
        EnumerateSystemHardwareMADT(MadtTableStart, MadtTableEnd);
        AcpiPutTable((ACPI_TABLE_HEADER*)MadtTable);
    }
    else {
        Result = OsError;
    }

    // Check for ECDT presence and enumerate. This table is not present on
    // any of the modern systems, they instead appear in the acpi namespace
    if (ACPI_SUCCESS(AcpiGetTable(ACPI_SIG_ECDT, 0, &Header))) {
        ACPI_TABLE_ECDT *EcdtTable = NULL;
        TRACE("Enumerating the ECDT Table");
        EcdtTable = (ACPI_TABLE_ECDT*)Header;
        //AcpiEnumerateECDT((void*)((uintptr_t)EcdtTable + sizeof(ACPI_TABLE_ECDT)),
        //    (void*)((uintptr_t)EcdtTable + EcdtTable->Header.Length));
        
        // Cleanup table when we are done with it as we are using
        // static pointers and reaollcating later
        AcpiPutTable(Header);
    }

    // Check for SBST presence and enumerate
    // @todo
    if (ACPI_SUCCESS(AcpiGetTable(ACPI_SIG_SBST, 0, &Header))) {
        ACPI_TABLE_SBST *BattTable = NULL;
        TRACE("Parsing the SBST Table");
        BattTable = (ACPI_TABLE_SBST*)Header;
        
        // Cleanup table when we are done with it as we are using
        // static pointers and reaollcating later
        AcpiPutTable(Header);
    }
    return Result;
}

/* This returns 0 if ACPI is not available
 * on the system, or 1 if acpi is available */
int AcpiAvailable(void)
{
    return AcpiStatus;
}

/**
 * Copyright 2021, Philip Meulengracht
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
 * Mostly just tested on QEmu
 */

#include <platform/arm64.h>
#include <platform/arm64-memory.h>
#include <console.h>
#include <Library/BaseMemoryLib.h>
#include <Library/BaseLib.h>
#include <Protocol/LoadedImage.h>
#include <Guid/Acpi.h>

/*
 * Verify that a physical range keeps the same address under the current
 * translation regime. The trampoline disables translation during handoff.
 */
BOOLEAN
Arm64Identity(
    IN UINT64 Base,
    IN UINT64 Length)
{
    UINT64 El;
    UINT64 Sctlr;
    UINT64 Par;
    UINT64 Page;

    // Reject wrapped ranges before using the end address in the page walk.
    if (!Length || Base + Length < Base) {
        return FALSE;
    }

    __asm__ volatile("mrs %0, CurrentEL" : "=r"(El));
    if (El == 4) {
        __asm__ volatile("mrs %0, sctlr_el1" : "=r"(Sctlr));
    } else if (El == 8) {
        __asm__ volatile("mrs %0, sctlr_el2" : "=r"(Sctlr));
    } else {
        // ARM64 UEFI runs this loader at EL1 or EL2.
        return FALSE;
    }

    // With the MMU off, virtual addresses are physical addresses.
    if (!(Sctlr & 1)) {
        return TRUE;
    }

    for (Page = Base & ~4095ULL; Page < Base + Length; Page += 4096) {
        // Use the current EL's translation regime. AT changes PAR_EL1, not the firmware tables.
        if (El == 4) {
            __asm__ volatile("at s1e1r, %0; isb" :: "r"(Page) : "memory");
        } else {
            __asm__ volatile("at s1e2r, %0; isb" :: "r"(Page) : "memory");
        }
        __asm__ volatile("mrs %0, par_el1" : "=r"(Par));

        // Reject faults and translations that change the address.
        if ((Par & 1) || (Par & 0x0000fffffffff000ULL) != Page) {
            return FALSE;
        }
        // Avoid overflow when advancing past the final page.
        if (Base + Length - Page <= EFI_PAGE_SIZE) {
            break;
        }
    }

    return TRUE;
}

// GIC version found in the MADT. It is only used by the loader, and Arm64Enter
// reads it before cleaning the caches. It is not passed on in VBoot, as the
// kernel reads and checks the MADT again before it uses the GIC.
UINT32 gArm64GicVersion;

/**
 * Check architectural capabilities without changing firmware's CPU controls.
 * Boot services still run after this check, so HCR and translation state must
 * remain as firmware configured them until Arm64Enter runs.
 */
EFI_STATUS
Arm64CheckCpu(
    VOID)
{
    UINT64 El;
    UINT64 Pfr0;
    UINT64 Mmfr0;
    UINT64 Mmfr1;
    UINT64 Mmfr2;
    UINT64 Sctlr;
    UINT64 Hcr;
    UINT64 Frequency;
    UINT64 El1;

    __asm__ volatile("mrs %0, CurrentEL" : "=r"(El));
    if (El != 4 && El != 8) {
        return EFI_UNSUPPORTED;
    }

    // Require AArch64 EL1 and 4 KiB translation granules.
    __asm__ volatile("mrs %0, id_aa64pfr0_el1" : "=r"(Pfr0));
    __asm__ volatile("mrs %0, id_aa64mmfr0_el1" : "=r"(Mmfr0));
    El1 = (Pfr0 >> 4) & 15;
    if ((El1 != 1 && El1 != 2) || ((Mmfr0 >> 28) & 15) > 1) {
        return EFI_UNSUPPORTED;
    }

    // The cache clean loop supports legacy and CCIDX layouts only.
    __asm__ volatile("mrs %0, id_aa64mmfr2_el1" : "=r"(Mmfr2));
    if (((Mmfr2 >> 20) & 15) > 1) {
        return EFI_UNSUPPORTED;
    }

    if (El == 8) {
        __asm__ volatile("mrs %0, sctlr_el2" : "=r"(Sctlr));
        __asm__ volatile("mrs %0, hcr_el2" : "=r"(Hcr));
        __asm__ volatile("mrs %0, id_aa64mmfr1_el1" : "=r"(Mmfr1));
        // With E2H set, EL1 register names access EL2 state. The trampoline uses EL12/EL02 instead.
        if ((Hcr & (1ULL << 34)) && ((Mmfr1 >> 8) & 15) != 1) {
            return EFI_UNSUPPORTED;
        }
    } else {
        __asm__ volatile("mrs %0, sctlr_el1" : "=r"(Sctlr));
    }

    // Require little-endian data and a usable architected timer.
    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(Frequency));
    if ((Sctlr & (1ULL << 25)) || !Frequency) {
        return EFI_UNSUPPORTED;
    }
    return EFI_SUCCESS;
}

/* Compare firmware signatures without depending on libc being available. */
static INTN
__CompareBytes(
    IN CONST VOID* Left,
    IN CONST VOID* Right,
    IN UINTN       Size)
{
    CONST UINT8* LeftBytes = Left;
    CONST UINT8* RightBytes = Right;

    for (UINTN Index = 0; Index < Size; ++Index) {
        if (LeftBytes[Index] != RightBytes[Index]) {
            return (INTN)LeftBytes[Index] - RightBytes[Index];
        }
    }

    return 0;
}

/* ACPI tables are valid only when their byte-wise sum wraps to zero. */
static BOOLEAN
__Checksum(
    IN CONST UINT8* Bytes,
    IN UINTN        Length)
{
    UINT8 Sum = 0;

    for (UINTN Index = 0; Index < Length; ++Index) {
        Sum += Bytes[Index];
    }

    return Sum == 0;
}

/* Read and validate an ACPI table header before walking its contents. */
static UINT32
__TableLength(
    IN const struct Arm64FirmwareMap* Map,
    IN UINT64                         Address)
{
    UINT32 Length;

    if (!Arm64AcpiRange(Map, Address, 36)) {
        return 0;
    }

    Length = ReadUnaligned32((UINT32*)(UINTN)(Address + 4));
    if (Length < 36 || Length > 0x100000 ||
        !Arm64AcpiRange(Map, Address, Length) ||
        !__Checksum((CONST UINT8*)(UINTN)Address, Length)) {
        return 0;
    }

    return Length;
}

static EFI_STATUS
__DiscoverGic(
    IN UINT64 Address,
    IN UINT32 Size)
{
    UINT32 Offset = 44;
    UINT8* Entry;

    if (Size < Offset) {
        return EFI_UNSUPPORTED;
    }

    while (Offset < Size) {
        if (Size - Offset < 2) {
            return EFI_UNSUPPORTED;
        }

        Entry = (UINT8*)(UINTN)(Address + Offset);
        if (Entry[1] < 2 || Entry[1] > Size - Offset) {
            return EFI_UNSUPPORTED;
        }

        if (Entry[0] == 12) {
            // Entry type 12 is the GIC distributor, and byte 20 of its 24 byte
            // record is the GIC version. We must not use the ICC registers just
            // because the CPU has them, as GICv2 still uses the MMIO interface.
            if (Entry[1] < 24 || gArm64GicVersion || (Entry[20] != 2 && Entry[20] != 3)) {
                return EFI_UNSUPPORTED;
            }
            gArm64GicVersion = Entry[20];
        }
        Offset += Entry[1];
    }
    return EFI_SUCCESS;
}

/*
 * Locate the FADT and derive the PSCI conduit selected by firmware. The
 * kernel needs this before SMP or power-management calls, and ARM64 EFI does
 * not provide the conduit as a separate boot protocol field.
 */
static EFI_STATUS
__DiscoverPsci(
    IN const struct Arm64FirmwareMap* Map,
    IN  UINT64                        AcpiRsdp,
    OUT UINT32*                       PsciConduit)
{
    UINT8*     Rsdp = (UINT8*)(UINTN)AcpiRsdp;
    UINT32     Length;
    UINT64     Xsdt;
    EFI_STATUS Status;

    gArm64GicVersion = 0;

    // Validate the full RSDP before reading its XSDT address.
    if (!Arm64AcpiRange(Map, AcpiRsdp, 36) ||
        __CompareBytes(Rsdp, "RSD PTR ", 8) || Rsdp[15] < 2 ||
        !__Checksum(Rsdp, 20)) {
        return EFI_UNSUPPORTED;
    }

    Length = ReadUnaligned32((UINT32*)(Rsdp + 20));
    if (Length < 36 || Length > 4096 ||
        !Arm64AcpiRange(Map, AcpiRsdp, Length) ||
        !__Checksum(Rsdp, Length)) {
        return EFI_UNSUPPORTED;
    }

    Xsdt = ReadUnaligned64((UINT64*)(Rsdp + 24));
    Length = __TableLength(Map, Xsdt);
    if (!Length || (Length - 36) % 8 ||
        __CompareBytes((CONST VOID*)(UINTN)Xsdt, "XSDT", 4)) {
        return EFI_UNSUPPORTED;
    }

    for (UINTN Offset = 36; Offset < Length; Offset += 8) {
        UINT64 Address = ReadUnaligned64((UINT64*)(UINTN)(Xsdt + Offset));
        UINT32 Size = __TableLength(Map, Address);

        if (!Size) {
            return EFI_UNSUPPORTED;
        }

        if (Size >= 132 && !__CompareBytes((CONST VOID*)(UINTN)Address, "FACP", 4)) {
            UINT16 Flags = ReadUnaligned16((UINT16*)(UINTN)(Address + 129));

            // Reject firmware that does not advertise PSCI support.
            if (!(Flags & 1)) {
                return EFI_UNSUPPORTED;
            }

            *PsciConduit = Flags & 2 ? VBOOT_PSCI_HVC : VBOOT_PSCI_SMC;
        }
        
        if (!__CompareBytes((CONST VOID*)(UINTN)Address, "APIC", 4)) {
            Status = __DiscoverGic(Address, Size);
            if (EFI_ERROR(Status)) {
                return Status;
            }
        }
    }
    return *PsciConduit && gArm64GicVersion ? EFI_SUCCESS : EFI_UNSUPPORTED;
}

EFI_STATUS
Arm64Prepare(
    IN struct VBoot* Boot)
{
    EFI_LOADED_IMAGE_PROTOCOL* Image;
    EFI_GUID                   Loaded = EFI_LOADED_IMAGE_PROTOCOL_GUID;
    EFI_GUID                   Acpi = EFI_ACPI_20_TABLE_GUID;

    struct Arm64FirmwareMap map;
    UINT64                  El;
    UINT64                  Midr;
    UINT64                  Features;
    UINT64                  Hcr;
    UINT64                  AcpiRsdp = 0;
    UINT32                  PsciConduit = 0;
    EFI_STATUS              Status;
    VOID*                   Tables;
    UINTN                   Bytes;

    __asm__ volatile("mrs %0, CurrentEL; mrs %1, midr_el1" : "=r"(El), "=r"(Midr));
    ConsoleWrite(L"ARM64 firmware EL%u, MIDR %lx\n", El >> 2, Midr);
    
    Status = Arm64CheckCpu();
    if (EFI_ERROR(Status)) {
        return Status;
    }

    if (El == 8) {
        __asm__ volatile("mrs %0, hcr_el2" : "=r"(Hcr));
        ConsoleWrite(L"ARM64 HCR %lx\n", Hcr);
    }

    Status = gBS->HandleProtocol(
        gImageHandle,
        &Loaded,
        (VOID**)&Image
    );
    if (EFI_ERROR(Status)) {
        return Status;
    }

    // Keep every handoff object identity mapped after ExitBootServices.
    if (!Arm64Identity((UINTN)Image->ImageBase, Image->ImageSize) ||
        !Arm64Identity((UINTN)Boot, sizeof(*Boot)) ||
        !Arm64Identity(Boot->Kernel.Base, Boot->Kernel.Length) ||
        !Arm64Identity(Boot->Stack.Base, Boot->Stack.Length) ||
        !Arm64Identity(Boot->Ramdisk.Data, Boot->Ramdisk.Length) ||
        !Arm64Identity(Boot->Phoenix.Base, Boot->Phoenix.Length)) {
        return EFI_UNSUPPORTED;
    }

    // The kernel discovers ACPI through VBoot's configuration table list.
    for (UINTN Index = 0; Index < gST->NumberOfTableEntries; ++Index) {
        EFI_CONFIGURATION_TABLE* Table = &gST->ConfigurationTable[Index];

        if (CompareGuid(&Table->VendorGuid, &Acpi)) {
            AcpiRsdp = (UINTN)Table->VendorTable;
        }
    }

    // Use this snapshot only to validate firmware tables. LibraryCleanup collects the final map.
    Status = Arm64ReadFirmwareMap(&map);
    if (EFI_ERROR(Status)) {
        return Status;
    }
    
    Status = __DiscoverPsci(&map, AcpiRsdp, &PsciConduit);
    LibraryFreeMemory(map.Descriptors);
    ConsoleWrite(L"ARM64 ACPI %lx, PSCI %u, status %r\n", AcpiRsdp, PsciConduit, Status);
    if (EFI_ERROR(Status)) {
        return Status;
    }
    
    if (gArm64GicVersion == 3) {
        // ICC_SRE is undefined if the CPU does not have the GIC system
        // registers, so check ID_AA64PFR0_EL1.GIC first. Firmware at EL2/EL3
        // must also allow these accesses, as we cannot change its traps from EL1.
        __asm__ volatile("mrs %0, id_aa64pfr0_el1" : "=r"(Features));
        if (!((Features >> 24) & 15)) {
            return EFI_UNSUPPORTED;
        }
    }

    if (El == 8 && PsciConduit != VBOOT_PSCI_SMC) {
        return EFI_UNSUPPORTED;
    }

    // Copy the table list before ExitBootServices invalidates the system table.
    Bytes = Boot->ConfigurationTableCount * sizeof(EFI_CONFIGURATION_TABLE);
    Status = LibraryAllocateMemory(Bytes, &Tables);
    if (EFI_ERROR(Status)) {
        return Status;
    }

    CopyMem(Tables, gST->ConfigurationTable, Bytes);
    
    if (!Arm64Identity((UINTN)Tables, Bytes)) {
        LibraryFreeMemory(Tables);
        return EFI_UNSUPPORTED;
    }

    Boot->ConfigurationTable = (UINTN)Tables;
    return EFI_SUCCESS;
}

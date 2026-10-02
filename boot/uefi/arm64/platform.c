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
#include <console.h>
#include <Library/BaseMemoryLib.h>
#include <Library/BaseLib.h>
#include <Library/SerialPortLib.h>
#include <Protocol/LoadedImage.h>
#include <Guid/Acpi.h>

/*
 * Verify that every page in a physical range is also reachable through the
 * current translation regime. The kernel is entered with the firmware's
 * translation state, so ranges that are valid physical addresses can still
 * fault during the handoff if firmware has mapped them elsewhere or not at
 * all.
 */
BOOLEAN Arm64Identity(
    IN UINT64 Base,
    IN UINT64 Length)
{
    UINT64 El;
    UINT64 Sctlr;
    UINT64 Par;

    /* Reject wrapped ranges before using the end address in the page walk. */
    if (!Length || Base + Length < Base) {
        return FALSE;
    }

    __asm__ volatile("mrs %0, CurrentEL" : "=r"(El));
    if (El == 4) {
        __asm__ volatile("mrs %0, sctlr_el1" : "=r"(Sctlr));
    } else if (El == 8) {
        __asm__ volatile("mrs %0, sctlr_el2" : "=r"(Sctlr));
    } else {
        /* The loader only knows how to inspect the levels used by ARM64 EFI. */
        return FALSE;
    }

    /* With translation disabled, the firmware already provides identity access. */
    if (!(Sctlr & 1)) {
        return TRUE;
    }

    for (UINT64 Page = Base & ~4095ULL; Page < Base + Length; Page += 4096) {
        /* AT checks the translation without changing architectural state. */
        if (El == 4) {
            __asm__ volatile("at s1e1r, %0; isb" :: "r"(Page) : "memory");
        } else {
            __asm__ volatile("at s1e2r, %0; isb" :: "r"(Page) : "memory");
        }
        __asm__ volatile("mrs %0, par_el1" : "=r"(Par));

        /* A fault or a translated physical address would break the handoff. */
        if ((Par & 1) || (Par & 0x0000fffffffff000ULL) != Page) {
            return FALSE;
        }
    }

    return TRUE;
}

/* Compare firmware signatures without depending on libc being available. */
static INTN __CompareBytes(
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

/*
 * Restrict firmware table reads to the address window exposed by the ARM64
 * platform. This prevents malformed configuration tables from making the
 * loader dereference arbitrary physical addresses before the kernel starts.
 */
static BOOLEAN __FirmwareRange(
    IN UINT64 Address,
    IN UINT64 Length)
{
    return Address >= 0x40000000 && Address < 0x80000000 &&
        Length <= 0x80000000 - Address && Arm64Identity(Address, Length);
}

/* ACPI tables are valid only when their byte-wise sum wraps to zero. */
static BOOLEAN __Checksum(
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
static UINT32 __TableLength(IN UINT64 Address)
{
    UINT32 Length;

    if (!__FirmwareRange(Address, 36)) {
        return 0;
    }

    Length = ReadUnaligned32((UINT32*)(UINTN)(Address + 4));
    if (Length < 36 || Length > 0x100000 ||
        !__FirmwareRange(Address, Length) ||
        !__Checksum((CONST UINT8*)(UINTN)Address, Length)) {
        return 0;
    }

    return Length;
}

/*
 * Locate the FADT and derive the PSCI conduit selected by firmware. The
 * kernel needs this before SMP or power-management calls, and ARM64 EFI does
 * not provide the conduit as a separate boot protocol field.
 */
static EFI_STATUS __DiscoverPsci(IN struct VBoot* Boot)
{
    UINT8* Rsdp = (UINT8*)(UINTN)Boot->AcpiRsdp;
    UINT32 Length;
    UINT64 Xsdt;

    /* The RSDP identifies the ACPI revision and the root table address. */
    if (!__FirmwareRange(Boot->AcpiRsdp, 36) ||
        __CompareBytes(Rsdp, "RSD PTR ", 8) || Rsdp[15] < 2 ||
        !__Checksum(Rsdp, 20)) {
        return EFI_UNSUPPORTED;
    }

    Length = ReadUnaligned32((UINT32*)(Rsdp + 20));
    if (Length < 36 || Length > 4096 ||
        !__FirmwareRange(Boot->AcpiRsdp, Length) ||
        !__Checksum(Rsdp, Length)) {
        return EFI_UNSUPPORTED;
    }

    Xsdt = ReadUnaligned64((UINT64*)(Rsdp + 24));
    Length = __TableLength(Xsdt);
    if (!Length || (Length - 36) % 8 ||
        __CompareBytes((CONST VOID*)(UINTN)Xsdt, "XSDT", 4)) {
        return EFI_UNSUPPORTED;
    }

    for (UINTN Offset = 36; Offset < Length; Offset += 8) {
        UINT64 Address = ReadUnaligned64((UINT64*)(UINTN)(Xsdt + Offset));
        UINT32 Size = __TableLength(Address);

        if (!Size) {
            return EFI_UNSUPPORTED;
        }

        if (Size >= 132 && !__CompareBytes((CONST VOID*)(UINTN)Address, "FACP", 4)) {
            UINT16 Flags = ReadUnaligned16((UINT16*)(UINTN)(Address + 129));

            /* The FADT flags declare whether firmware supports PSCI at all. */
            if (!(Flags & 1)) {
                return EFI_UNSUPPORTED;
            }

            Boot->PsciConduit = Flags & 2 ? VBOOT_PSCI_HVC : VBOOT_PSCI_SMC;
            return EFI_SUCCESS;
        }
    }

    return EFI_UNSUPPORTED;
}

EFI_STATUS Arm64Prepare(IN struct VBoot* Boot)
{
    UINT64 El;
    UINT64 Midr;
    UINT64 Hcr;
    EFI_LOADED_IMAGE_PROTOCOL* Image;
    EFI_GUID Loaded = EFI_LOADED_IMAGE_PROTOCOL_GUID;
    EFI_GUID Acpi = EFI_ACPI_20_TABLE_GUID;
    /* SMBIOS 3 GUID, normalized to its physical entry-point address. */
    EFI_GUID Smbios = {
        0xf2fd1544, 0x9794, 0x4a2c,
        {0x99, 0x2e, 0xe5, 0xbb, 0xcf, 0x20, 0xe3, 0x94}
    };

    __asm__ volatile("mrs %0, CurrentEL; mrs %1, midr_el1" : "=r"(El), "=r"(Midr));
    ConsoleWrite(L"ARM64 firmware EL%u, MIDR %lx\n", El >> 2, Midr);
    if ((El != 4 && El != 8) || ((Midr >> 4) & 0xfff) != 0xd07) {
        return EFI_UNSUPPORTED;
    }

    if (El == 8) {
        __asm__ volatile("mrs %0, hcr_el2" : "=r"(Hcr));
        ConsoleWrite(L"ARM64 HCR %lx\n", Hcr);
        /* HCR.TGE would make the EL1 translation checks observe the wrong regime. */
        if (Hcr & (1ULL << 34)) {
            return EFI_UNSUPPORTED;
        }
    }

    EFI_STATUS Status = gBS->HandleProtocol(
        gImageHandle,
        &Loaded,
        (VOID**)&Image
    );
    if (EFI_ERROR(Status)) {
        return Status;
    }

    /* Every object passed after ExitBootServices must remain directly reachable. */
    if (!Arm64Identity((UINTN)Image->ImageBase, Image->ImageSize) ||
        !Arm64Identity((UINTN)Boot, sizeof(*Boot)) ||
        !Arm64Identity(Boot->Kernel.Base, Boot->Kernel.Length) ||
        !Arm64Identity(Boot->Stack.Base, Boot->Stack.Length) ||
        !Arm64Identity(Boot->Ramdisk.Data, Boot->Ramdisk.Length) ||
        !Arm64Identity(Boot->Phoenix.Base, Boot->Phoenix.Length)) {
        return EFI_UNSUPPORTED;
    }

    /* Describe the physical, identity-mapped handoff contract to the kernel. */
    Boot->Version = VBOOT_VERSION;
    Boot->DescriptorSize = sizeof(*Boot);
    Boot->Architecture = VBOOT_ARCH_AARCH64;
    Boot->Features = VBOOT_FEATURE_PHYSICAL | VBOOT_FEATURE_MMU_OFF | VBOOT_FEATURE_RESERVED_FIRMWARE;
    
    if (Boot->Video.FrameBuffer) {
        Boot->Features |= VBOOT_FEATURE_GOP;
    }
    
    Boot->MemoryEntrySize = sizeof(struct VBootMemoryEntry);
    Boot->ConfigurationEntrySize = sizeof(EFI_CONFIGURATION_TABLE);
    Boot->Console.Base = 0x09000000;
    Boot->Console.Kind = VBOOT_CONSOLE_PL011;
    Boot->Console.ClockHz = 24000000;
    Boot->Console.Baud = 115200;

    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(Boot->CounterFrequency));
    Boot->LoaderBase = (UINTN)Image->ImageBase;
    Boot->LoaderLength = Image->ImageSize;
    for (UINTN Index = 0; Index < gST->NumberOfTableEntries; ++Index) {
        EFI_CONFIGURATION_TABLE* Table = &gST->ConfigurationTable[Index];

        if (CompareGuid(&Table->VendorGuid, &Acpi)) {
            Boot->AcpiRsdp = (UINTN)Table->VendorTable;
        }
        if (CompareGuid(&Table->VendorGuid, &Smbios)) {
            Boot->Smbios = (UINTN)Table->VendorTable;
        }
    }

    Status = __DiscoverPsci(Boot);
    ConsoleWrite(L"ARM64 ACPI %lx, PSCI %u, status %r\n", Boot->AcpiRsdp, Boot->PsciConduit, Status);
    if (EFI_ERROR(Status)) {
        return Status;
    }
    if (El == 8 && Boot->PsciConduit != VBOOT_PSCI_SMC) {
        return EFI_UNSUPPORTED;
    }

    /* Copy the table list because the EFI system table ceases to be usable later. */
    VOID* Tables;
    UINTN Bytes = Boot->ConfigurationTableCount * sizeof(EFI_CONFIGURATION_TABLE);
    Status = LibraryAllocateMemory(Bytes, &Tables);
    if (EFI_ERROR(Status)) {
        return Status;
    }
    CopyMem(Tables, gST->ConfigurationTable, Bytes);
    if (!Arm64Identity((UINTN)Tables, Bytes)) {
        return EFI_UNSUPPORTED;
    }
    Boot->ConfigurationTable = (UINTN)Tables;
    return EFI_SUCCESS;
}

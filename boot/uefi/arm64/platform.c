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

BOOLEAN Arm64Identity(UINT64 Base, UINT64 Length)
{
    UINT64 el, sctlr, par;
    if (!Length || Base + Length < Base) return FALSE;
    __asm__ volatile("mrs %0, CurrentEL" : "=r"(el));
    if (el == 4) __asm__ volatile("mrs %0, sctlr_el1" : "=r"(sctlr));
    else if (el == 8) __asm__ volatile("mrs %0, sctlr_el2" : "=r"(sctlr));
    else return FALSE;
    if (!(sctlr & 1)) return TRUE;
    for (UINT64 p = Base & ~4095ULL; p < Base + Length; p += 4096) {
        if (el == 4) __asm__ volatile("at s1e1r, %0; isb" :: "r"(p) : "memory");
        else __asm__ volatile("at s1e2r, %0; isb" :: "r"(p) : "memory");
        __asm__ volatile("mrs %0, par_el1" : "=r"(par));
        if ((par & 1) || (par & 0x0000fffffffff000ULL) != p) return FALSE;
    }
    return TRUE;
}

static INTN CompareBytes(const VOID* Left, const VOID* Right, UINTN Size)
{
    const UINT8* left = Left;
    const UINT8* right = Right;
    for (UINTN i = 0; i < Size; ++i)
        if (left[i] != right[i]) return (INTN)left[i] - right[i];
    return 0;
}

static BOOLEAN FirmwareRange(UINT64 address, UINT64 length)
{
    return address >= 0x40000000 && address < 0x80000000 &&
        length <= 0x80000000 - address && Arm64Identity(address, length);
}

static BOOLEAN Checksum(const UINT8* bytes, UINTN length)
{
    UINT8 sum = 0;
    for (UINTN i = 0; i < length; ++i) sum += bytes[i];
    return sum == 0;
}

static UINT32 TableLength(UINT64 address)
{
    if (!FirmwareRange(address, 36)) return 0;
    UINT32 length = ReadUnaligned32((UINT32*)(UINTN)(address + 4));
    if (length < 36 || length > 0x100000 || !FirmwareRange(address, length) ||
        !Checksum((UINT8*)(UINTN)address, length)) return 0;
    return length;
}

static EFI_STATUS DiscoverPsci(struct VBoot* Boot)
{
    UINT8* rsdp = (UINT8*)(UINTN)Boot->AcpiRsdp;
    if (!FirmwareRange(Boot->AcpiRsdp, 36) || CompareBytes(rsdp, "RSD PTR ", 8) ||
        rsdp[15] < 2 || !Checksum(rsdp, 20)) return EFI_UNSUPPORTED;
    UINT32 length = ReadUnaligned32((UINT32*)(rsdp + 20));
    if (length < 36 || length > 4096 || !FirmwareRange(Boot->AcpiRsdp, length) ||
        !Checksum(rsdp, length)) return EFI_UNSUPPORTED;
    UINT64 xsdt = ReadUnaligned64((UINT64*)(rsdp + 24));
    length = TableLength(xsdt);
    if (!length || (length - 36) % 8 || CompareBytes((VOID*)(UINTN)xsdt, "XSDT", 4)) return EFI_UNSUPPORTED;
    for (UINTN offset = 36; offset < length; offset += 8) {
        UINT64 address = ReadUnaligned64((UINT64*)(UINTN)(xsdt + offset));
        UINT32 size = TableLength(address);
        if (!size) return EFI_UNSUPPORTED;
        if (size >= 132 && !CompareBytes((VOID*)(UINTN)address, "FACP", 4)) {
            UINT16 flags = ReadUnaligned16((UINT16*)(UINTN)(address + 129));
            if (!(flags & 1)) return EFI_UNSUPPORTED;
            Boot->PsciConduit = flags & 2 ? VBOOT_PSCI_HVC : VBOOT_PSCI_SMC;
            return EFI_SUCCESS;
        }
    }
    return EFI_UNSUPPORTED;
}

EFI_STATUS Arm64Prepare(struct VBoot* Boot)
{
    UINT64 el, midr, hcr;
    EFI_LOADED_IMAGE_PROTOCOL* image;
    EFI_GUID loaded = EFI_LOADED_IMAGE_PROTOCOL_GUID;
    EFI_GUID acpi = EFI_ACPI_20_TABLE_GUID;
    /* SMBIOS 3 GUID, normalized to its physical entry-point address. */
    EFI_GUID smbios = {0xf2fd1544,0x9794,0x4a2c,{0x99,0x2e,0xe5,0xbb,0xcf,0x20,0xe3,0x94}};
    __asm__ volatile("mrs %0, CurrentEL; mrs %1, midr_el1" : "=r"(el), "=r"(midr));
    ConsoleWrite(L"ARM64 firmware EL%u, MIDR %lx\n", el >> 2, midr);
    if ((el != 4 && el != 8) || ((midr >> 4) & 0xfff) != 0xd07) return EFI_UNSUPPORTED;
    if (el == 8) {
        __asm__ volatile("mrs %0, hcr_el2" : "=r"(hcr));
        ConsoleWrite(L"ARM64 HCR %lx\n", hcr);
        if (hcr & (1ULL << 34)) return EFI_UNSUPPORTED;
    }
    EFI_STATUS status = gBS->HandleProtocol(gImageHandle, &loaded, (VOID**)&image);
    if (EFI_ERROR(status)) return status;
    if (!Arm64Identity((UINTN)image->ImageBase, image->ImageSize) ||
        !Arm64Identity((UINTN)Boot, sizeof(*Boot)) ||
        !Arm64Identity(Boot->Kernel.Base, Boot->Kernel.Length) ||
        !Arm64Identity(Boot->Stack.Base, Boot->Stack.Length) ||
        !Arm64Identity(Boot->Ramdisk.Data, Boot->Ramdisk.Length) ||
        !Arm64Identity(Boot->Phoenix.Base, Boot->Phoenix.Length)) return EFI_UNSUPPORTED;
    Boot->Version = VBOOT_VERSION;
    Boot->DescriptorSize = sizeof(*Boot);
    Boot->Architecture = VBOOT_ARCH_AARCH64;
    Boot->Features = VBOOT_FEATURE_PHYSICAL | VBOOT_FEATURE_MMU_OFF | VBOOT_FEATURE_RESERVED_FIRMWARE;
    if (Boot->Video.FrameBuffer) Boot->Features |= VBOOT_FEATURE_GOP;
    Boot->MemoryEntrySize = sizeof(struct VBootMemoryEntry);
    Boot->ConfigurationEntrySize = sizeof(EFI_CONFIGURATION_TABLE);
    Boot->Console.Base = 0x09000000;
    Boot->Console.Kind = VBOOT_CONSOLE_PL011;
    Boot->Console.ClockHz = 24000000;
    Boot->Console.Baud = 115200;

    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(Boot->CounterFrequency));
    Boot->LoaderBase = (UINTN)image->ImageBase;
    Boot->LoaderLength = image->ImageSize;
    for (UINTN i = 0; i < gST->NumberOfTableEntries; ++i) {
        EFI_CONFIGURATION_TABLE* t = &gST->ConfigurationTable[i];
        if (CompareGuid(&t->VendorGuid, &acpi)) Boot->AcpiRsdp = (UINTN)t->VendorTable;
        if (CompareGuid(&t->VendorGuid, &smbios)) Boot->Smbios = (UINTN)t->VendorTable;
    }
    status = DiscoverPsci(Boot);
    ConsoleWrite(L"ARM64 ACPI %lx, PSCI %u, status %r\n", Boot->AcpiRsdp, Boot->PsciConduit, status);
    if (EFI_ERROR(status)) return status;
    if (el == 8 && Boot->PsciConduit != VBOOT_PSCI_SMC) return EFI_UNSUPPORTED;
    VOID* tables;
    UINTN bytes = Boot->ConfigurationTableCount * sizeof(EFI_CONFIGURATION_TABLE);
    status = LibraryAllocateMemory(bytes, &tables);
    if (EFI_ERROR(status)) return status;
    CopyMem(tables, gST->ConfigurationTable, bytes);
    if (!Arm64Identity((UINTN)tables, bytes)) return EFI_UNSUPPORTED;
    Boot->ConfigurationTable = (UINTN)tables;
    return EFI_SUCCESS;
}

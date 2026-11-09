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

#include <platform/arm64.h>
#include <platform/arm64-memory.h>

EFI_STATUS
Arm64ReadFirmwareMap(
    OUT struct Arm64FirmwareMap* Map)
{
    EFI_STATUS status;
    UINTN      key;
    UINTN      capacity;
    UINT32     version;

    // Start with an empty map.
    SetMem(Map, sizeof(struct Arm64FirmwareMap), 0);

    status = gBS->GetMemoryMap(&Map->Size, NULL, &key, &Map->Stride, &version);
    if (status != EFI_BUFFER_TOO_SMALL || Map->Stride < sizeof(EFI_MEMORY_DESCRIPTOR)) {
        return EFI_UNSUPPORTED;
    }

    for (UINTN attempt = 0; attempt < 8; attempt++) {
        // Leave room for descriptors added by this allocation.
        if (Map->Stride > MAX_UINTN / 8 || Map->Size > MAX_UINTN - 8 * Map->Stride) {
            return EFI_OUT_OF_RESOURCES;
        }

        capacity = Map->Size + 8 * Map->Stride;
        status = LibraryAllocateMemory(capacity, (VOID**)&Map->Descriptors);
        if (EFI_ERROR(status)) {
            return status;
        }
        
        Map->Size = capacity;
        status = gBS->GetMemoryMap(&Map->Size, Map->Descriptors, &key, &Map->Stride, &version);
        if (!EFI_ERROR(status)) {
            if (Map->Stride >= sizeof(EFI_MEMORY_DESCRIPTOR) && version == 1 &&
                Map->Size <= capacity && Map->Size % Map->Stride == 0) {
                return EFI_SUCCESS;
            }
            status = EFI_UNSUPPORTED;
        }

        LibraryFreeMemory(Map->Descriptors);
        Map->Descriptors = NULL;
        if (status != EFI_BUFFER_TOO_SMALL) {
            return status;
        }
    }
    return EFI_OUT_OF_RESOURCES;
}

BOOLEAN
Arm64AcpiRange(
    IN const struct Arm64FirmwareMap* Map,
    IN UINT64                         Address,
    IN UINT64                         Length)
{
    const EFI_MEMORY_DESCRIPTOR* descriptor;
    UINT64                       end;
    UINT64                       current;

    // Reject empty or wrapped ranges before walking descriptors.
    if (!Length || Length > MAX_UINT64 - Address) {
        return FALSE;
    }

    end = Address + Length;
    current = Address;
    
    while (current < end) {
        UINT64 next = current;
        
        for (UINTN offset = 0; offset < Map->Size; offset += Map->Stride) {
            UINT64 bytes;

            descriptor = (const VOID*)((const UINT8*)Map->Descriptors + offset);
            if (descriptor->Type != EfiACPIReclaimMemory && descriptor->Type != EfiACPIMemoryNVS) {
                continue;
            }
            
            if (descriptor->Attribute & EFI_MEMORY_RP) {
                continue;
            }
            
            if (descriptor->NumberOfPages > MAX_UINT64 / EFI_PAGE_SIZE) {
                return FALSE;
            }
            
            bytes = descriptor->NumberOfPages * EFI_PAGE_SIZE;
            if (bytes > MAX_UINT64 - descriptor->PhysicalStart) {
                return FALSE;
            }
            
            if (current >= descriptor->PhysicalStart && current - descriptor->PhysicalStart < bytes) {
                next = descriptor->PhysicalStart + bytes;
                break;
            }
        }

        // ACPI tables can span descriptors, so check coverage one range at a time.
        if (next == current) {
            return FALSE;
        }
        current = next;
    }

    // Check the full range before reading the ACPI table.
    return Arm64Identity(Address, Length);
}

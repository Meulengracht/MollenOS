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

#include "loader.h"
#include <fdt/reader.h>
#include <string.h>

static uint64_t
__ReadCells(
    _In_ const unsigned char* bytes,
    _In_ unsigned int         cells)
{
    uint64_t value = FdtReadBe32(bytes);

    if (cells == 2) {
        value = (value << 32) | FdtReadBe32(bytes + 4);
    }
    return value;
}

static void
__WriteWord(
    _Out_ unsigned char* bytes,
    _In_  uint32_t       value)
{
    bytes[0] = value >> 24;
    bytes[1] = value >> 16;
    bytes[2] = value >> 8;
    bytes[3] = value;
}

static void
__WriteCells(
    _Out_ unsigned char* bytes,
    _In_  unsigned int   cells,
    _In_  uint64_t       value)
{
    if (cells == 2) {
        __WriteWord(bytes, value >> 32);
        bytes += 4;
    }
    __WriteWord(bytes, value);
}

static uint64_t
__FindStorage(
    _In_ const struct RpiBootContext* context,
    _In_ uint64_t                     length,
    _In_ uint64_t                     alignment,
    _In_ uint64_t                     minimum,
    _In_ uint64_t                     maximum)
{
    // Prefer high RAM, leaving low memory for the kernel's placement policy.
    // All endpoints have already been validated as non-wrapping intervals.
    for (uint32_t i = context->MemoryMapCount; i > 0; i--) {
        const struct VBootMemoryEntry* entry = &context->MemoryMap[i - 1];

        uint64_t start = entry->PhysicalBase;
        uint64_t end = start + entry->Length;
        uint64_t candidate;

        if (entry->Type != VBootMemoryType_Available) {
            continue;
        }

        // Clamp the start and end addresses to the allowed minimum and maximum.
        if (start < minimum) {
            start = minimum;
        }
        if (end > maximum) {
            end = maximum;
        }

        // Check endpoint order before subtraction; the entire request must fit.
        if (end <= start || end - start < length) {
            continue;
        }
        
        candidate = (end - length) & ~(alignment - 1);
        // Rounding down must keep the allocation above its allowed lower bound.
        if (candidate >= start && candidate) {
            return candidate;
        }
    }
    return 0;
}

static oserr_t
__AllocateReservation(
    _In_ struct RpiBootContext*        context,
    _In_ struct RpiDynamicReservation* reservation)
{
    uint64_t length = (reservation->Length + RPI_PAGE_MASK) & ~RPI_PAGE_MASK;

    // A single address cell cannot encode a base above the 32-bit boundary.
    uint64_t     limit = reservation->AddressCells == 1 ? (uint64_t)UINT32_MAX + 1 : UINT64_MAX;
    uint64_t     base = 0;
    unsigned int stride = (reservation->AddressCells + reservation->SizeCells) * 4;

    if (!reservation->AllocRangesLength) {
        base = __FindStorage(context, length, reservation->Alignment, 0, limit);
    } else {
        for (unsigned int offset = 0; offset < reservation->AllocRangesLength; offset += stride) {
            const unsigned char* range = reservation->AllocRanges + offset;
            uint64_t minimum = __ReadCells(range, reservation->AddressCells);
            uint64_t maximum = minimum + __ReadCells(range + reservation->AddressCells * 4,
                reservation->SizeCells);

            if (maximum > limit) {
                maximum = limit;
            }
            
            base = __FindStorage(context, length, reservation->Alignment, minimum, maximum);
            if (base) {
                break;
            }
        }
    }
    
    if (!base) {
        return OS_EOOM;
    }
    
    reservation->PhysicalBase = base;
    return DeviceTreeReserveMemoryWithAttributes(
        context,
        base,
        length,
        reservation->Attributes
    );
}

static uint32_t
__WriteReservationProperty(
    _Out_ unsigned char*                      output,
    _In_  const struct RpiDynamicReservation* reservation,
    _In_  uint32_t                            nameOffset)
{
    uint32_t length = (reservation->AddressCells + reservation->SizeCells) * sizeof(uint32_t);
    uint32_t headerSize = sizeof(uint32_t) + sizeof(struct FDTProperty);

    // A property token precedes the length/name-offset header and cell data.
    __WriteWord(output, FDT_PROP);
    __WriteWord(output + sizeof(uint32_t) + offsetof(struct FDTProperty, Length), length);
    __WriteWord(output + sizeof(uint32_t) + offsetof(struct FDTProperty, NameOffset), nameOffset);
    
    __WriteCells(output + headerSize, reservation->AddressCells, reservation->PhysicalBase);
    __WriteCells(output + headerSize + reservation->AddressCells * sizeof(uint32_t),
        reservation->SizeCells, reservation->Length);
    
    return headerSize + length;
}

oserr_t
DeviceTreeResolveReservations(
    _In_ struct RpiBootContext* context,
    _In_ uint32_t               dtbLength)
{
    const unsigned char* source = (void*)(uintptr_t)context->DtbPhysical;
    struct FDTHeader     header;
    uint32_t             added = 0;
    uint32_t             total;
    uint32_t             outputOffset;
    uint32_t             sourceOffset;
    uint64_t             destination;
    unsigned char*       output;
    oserr_t              status;

    // Without dynamic requests, the original firmware tree already describes
    // the reserved ranges. Keep using it rather than allocating an identical copy.
    context->BootInformation.DeviceTree.PhysicalBase = context->DtbPhysical;
    context->BootInformation.DeviceTree.Length = dtbLength;
    if (!context->ReservationCount) {
        return OS_EOK;
    }
    
    // We need validated block offsets to copy the tree without reading past
    // its end. The decoded header also tells us which sizes must change later.
    status = FdtParseHeader(source, dtbLength, &header);
    if (status != OS_EOK) {
        return status;
    }
    
    // A size-only request does not tell the kernel which physical RAM it owns.
    // Choose and reserve that RAM now, before allocating the tree copy, so the
    // copy cannot consume memory already selected for one of these requests.
    for (uint32_t i = 0; i < context->ReservationCount; i++) {
        struct RpiDynamicReservation* reservation = &context->Reservations[i];

        status = __AllocateReservation(context, reservation);
        if (status != OS_EOK) {
            return status;
        }
        // Each request needs a new reg property containing its chosen address
        // and requested length. Include the token and property header as well
        // as the address/length cells when budgeting space for the copy.
        added += sizeof(uint32_t) + sizeof(struct FDTProperty) +
            (reservation->AddressCells + reservation->SizeCells) * sizeof(uint32_t);
    }
    
    // Keep everything before the node block, enlarge that block for the new
    // properties, then place the existing names and one shared "reg" name after
    // it. Include the terminating NUL so the new name is a complete string.
    total = header.OffDtStruct + header.SizeDtStruct + added + header.SizeDtStrings + sizeof("reg");
    // Do not let the rewritten tree exceed the loader's supported size limit.
    if (total > RPI_DTB_MAX_SIZE) {
        return OS_EBUFFER;
    }
    
    // Do not edit the firmware tree in place: discovery still holds pointers
    // into its bytes, and inserting properties would move those bytes. Allocate
    // a separate copy in whole pages so the kernel cannot reuse part of its RAM.
    destination = __FindStorage(
        context,
        (total + RPI_PAGE_MASK) & ~RPI_PAGE_MASK,
        RPI_PAGE_SIZE,
        0,
        UINT64_MAX
    );
    if (!destination) {
        return OS_EOOM;
    }
    
    // Record ownership before writing the copy. Finding free RAM alone does
    // not protect it from later loader or kernel allocations. The original
    // firmware tree stays reserved too; this operation does not reclaim it.
    status = DeviceTreeReserveMemory(
        context,
        destination,
        (total + RPI_PAGE_MASK) & ~RPI_PAGE_MASK
    );
    if (status != OS_EOK) {
        return status;
    }
    
    // Preserve the header, reservation block and any padding before the nodes.
    // Their positions do not change because new bytes are inserted only later.
    output = (void*)(uintptr_t)destination;
    memcpy(output, source, header.OffDtStruct);
    sourceOffset = header.OffDtStruct;
    outputOffset = header.OffDtStruct;

    // Discovery recorded requests in the order their nodes appear in the tree.
    // Copy the unchanged bytes up to each insertion point, then add its reg
    // property immediately after the node name. Properties must precede child
    // nodes, and this lets the kernel read a concrete address instead of having
    // to allocate the same size-only request again.
    for (uint32_t i = 0; i < context->ReservationCount; i++) {
        const struct RpiDynamicReservation* reservation = &context->Reservations[i];
        uint32_t insertion = (uint32_t)(reservation->InsertBefore - source);

        memcpy(output + outputOffset, source + sourceOffset, insertion - sourceOffset);
        outputOffset += insertion - sourceOffset;
        sourceOffset = insertion;
        outputOffset += __WriteReservationProperty(output + outputOffset,
            reservation, header.SizeDtStrings);
    }
    
    // Preserve the rest of the node block after the last insertion. Property
    // names live in a separate block, which now follows the enlarged node block.
    memcpy(output + outputOffset, source + sourceOffset,
        header.OffDtStruct + header.SizeDtStruct - sourceOffset);
    outputOffset = header.OffDtStruct + header.SizeDtStruct + added;
    memcpy(output + outputOffset, source + header.OffDtStrings, header.SizeDtStrings);
    // Appending one shared name keeps existing name offsets valid. Every new
    // property uses the old name-block size as the offset of this added string.
    memcpy(output + outputOffset + header.SizeDtStrings, "reg", sizeof("reg"));
    
    // Tell later parsers where the moved name block starts and how much each
    // enlarged block contains. Write the fields in the DTB's big-endian format.
    __WriteWord(output + offsetof(struct FDTHeader, TotalSize), total);
    __WriteWord(output + offsetof(struct FDTHeader, OffDtStrings), outputOffset);
    __WriteWord(output + offsetof(struct FDTHeader, SizeDtStrings), header.SizeDtStrings + sizeof("reg"));
    __WriteWord(output + offsetof(struct FDTHeader, SizeDtStruct), header.SizeDtStruct + added);
    
    // Switch to the copy only after all its bytes and header fields are ready.
    // The kernel will receive this tree and the memory map that protects it.
    context->DtbPhysical = destination;
    context->BootInformation.DeviceTree.PhysicalBase = destination;
    context->BootInformation.DeviceTree.Length = total;
    return OS_EOK;
}

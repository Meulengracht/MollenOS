/**
 * Copyright 2018, Philip Meulengracht
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
 * PE/COFF Image Loader
 *    - Implements support for loading and processing pe/coff image formats
 *      and implemented as a part of libds to share between services and kernel
 */

#include <ds/ds.h>
#include "pe.h"

uint32_t
PeCalculateChecksum(
    _In_ uint8_t* Data,
    _In_ size_t   DataLength,
    _In_ size_t   PeChkSumOffset)
{
    uint32_t* DataPtr  = (uint32_t*)Data;
    uint64_t  Limit    = 4294967296;
    uint64_t  CheckSum = 0;

    for (size_t i = 0; i < (DataLength / 4); i++, DataPtr++) {
        uint32_t Val = *DataPtr;

        // Skip the checksum index
        if (i == (PeChkSumOffset / 4)) {
            continue;
        }
        CheckSum = (CheckSum & UINT32_MAX) + Val + (CheckSum >> 32);
        if (CheckSum > Limit) {
            CheckSum = (CheckSum & UINT32_MAX) + (CheckSum >> 32);
        }
    }

    CheckSum = (CheckSum & UINT16_MAX) + (CheckSum >> 16);
    CheckSum = (CheckSum) + (CheckSum >> 16);
    CheckSum = CheckSum & UINT16_MAX;
    CheckSum += (uint32_t)DataLength;
    return (uint32_t)(CheckSum & UINT32_MAX);
}

OsStatus_t
PeValidateImageBuffer(
    _In_ uint8_t* Buffer,
    _In_ size_t   Length)
{
    PeOptionalHeader_t* OptHeader;
    PeHeader_t*         BaseHeader;
    MzHeader_t*         DosHeader;
    size_t              HeaderCheckSum     = 0;
    size_t              CalculatedCheckSum = 0;
    size_t              CheckSumAddress    = 0;

    if (Buffer == NULL || Length == 0) {
        return OsInvalidParameters;
    }
    DosHeader = (MzHeader_t*)Buffer;

    // Check magic for DOS
    if (DosHeader->Signature != MZ_MAGIC) {
        dserror("Invalid MZ Signature 0x%x", DosHeader->Signature);
        return OsError;
    }
    BaseHeader = (PeHeader_t*)(Buffer + DosHeader->PeHeaderAddress);

    // Check magic for PE
    if (BaseHeader->Magic != PE_MAGIC) {
        dserror("Invalid PE File Magic 0x%x", BaseHeader->Magic);
        return OsError;
    }
    OptHeader = (PeOptionalHeader_t*)(Buffer + DosHeader->PeHeaderAddress + sizeof(PeHeader_t));

    // Ok, time to validate the contents of the file
    // by performing a checksum of the PE file
    // We need to re-cast based on architecture
    if (OptHeader->Architecture == PE_ARCHITECTURE_32) {
        PeOptionalHeader32_t *OptHeader32 = 
            (PeOptionalHeader32_t*)(Buffer + DosHeader->PeHeaderAddress + sizeof(PeHeader_t));
        CheckSumAddress = (size_t)&(OptHeader32->ImageChecksum);
        HeaderCheckSum = OptHeader32->ImageChecksum;
    }
    else if (OptHeader->Architecture == PE_ARCHITECTURE_64) {
        PeOptionalHeader64_t *OptHeader64 = 
            (PeOptionalHeader64_t*)(Buffer + DosHeader->PeHeaderAddress + sizeof(PeHeader_t));
        CheckSumAddress = (size_t)&(OptHeader64->ImageChecksum);
        HeaderCheckSum = OptHeader64->ImageChecksum;
    }

    // Now do the actual checksum calc if the checksum
    // of the PE header is not 0
    if (HeaderCheckSum != 0) {
        dstrace("Checksum validation phase");
        CalculatedCheckSum = PeCalculateChecksum(
            Buffer, Length, CheckSumAddress - ((size_t)Buffer));
        if (CalculatedCheckSum != HeaderCheckSum) {
            dserror("Invalid checksum of file (Header 0x%x, Calculated 0x%x)", 
                HeaderCheckSum, CalculatedCheckSum);
            return OsError;
        }
    }
    return OsSuccess;
}

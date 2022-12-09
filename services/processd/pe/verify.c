/**
 * Copyright 2022, Philip Meulengracht
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
 */

#define __TRACE

#include <ddk/utils.h>
#include "pe.h"

static uint32_t
__CalculateChecksum(
    _In_ uint8_t* Data,
    _In_ size_t   DataLength,
    _In_ size_t   PeChkSumOffset)
{
    uint32_t* ptr  = (uint32_t*)Data;
    uint64_t  limit    = 4294967296;
    uint64_t  checkSum = 0;

    for (size_t i = 0; i < (DataLength / 4); i++, ptr++) {
        uint32_t _val = *ptr;

        // Skip the checksum index
        if (i == (PeChkSumOffset / 4)) {
            continue;
        }
        checkSum = (checkSum & UINT32_MAX) + _val + (checkSum >> 32);
        if (checkSum > limit) {
            checkSum = (checkSum & UINT32_MAX) + (checkSum >> 32);
        }
    }

    checkSum = (checkSum & UINT16_MAX) + (checkSum >> 16);
    checkSum = (checkSum) + (checkSum >> 16);
    checkSum = checkSum & UINT16_MAX;
    checkSum += (uint32_t)DataLength;
    return (uint32_t)(checkSum & UINT32_MAX);
}

oserr_t
PEValidateImageChecksum(
        _In_  uint8_t*  buffer,
        _In_  size_t    length,
        _Out_ uint32_t* checksumOut)
{
    PeOptionalHeader_t* optionalHeader;
    PeHeader_t*         peHeader;
    MzHeader_t*         dosHeader;
    size_t              calculatedCheckSum;
    size_t              headerCheckSum  = 0;
    size_t              checkSumAddress = 0;

    if (buffer == NULL || length == 0) {
        return OS_EINVALPARAMS;
    }
    dosHeader = (MzHeader_t*)buffer;

    // Check magic for DOS
    if (dosHeader->Signature != MZ_MAGIC) {
        ERROR("Invalid MZ Signature 0x%x", dosHeader->Signature);
        return OS_EUNKNOWN;
    }
    peHeader = (PeHeader_t*)(buffer + dosHeader->PeHeaderAddress);

    // Check magic for PE
    if (peHeader->Magic != PE_MAGIC) {
        ERROR("Invalid PE File Magic 0x%x", peHeader->Magic);
        return OS_EUNKNOWN;
    }
    optionalHeader = (PeOptionalHeader_t*)(buffer + dosHeader->PeHeaderAddress + sizeof(PeHeader_t));

    // Ok, time to validate the contents of the file
    // by performing a checksum of the PE file
    // We need to re-cast based on architecture
    if (optionalHeader->Architecture == PE_ARCHITECTURE_32) {
        PeOptionalHeader32_t *OptHeader32 = 
            (PeOptionalHeader32_t*)(buffer + dosHeader->PeHeaderAddress + sizeof(PeHeader_t));
        checkSumAddress = (size_t)&(OptHeader32->ImageChecksum);
        headerCheckSum = OptHeader32->ImageChecksum;
    }
    else if (optionalHeader->Architecture == PE_ARCHITECTURE_64) {
        PeOptionalHeader64_t *OptHeader64 = 
            (PeOptionalHeader64_t*)(buffer + dosHeader->PeHeaderAddress + sizeof(PeHeader_t));
        checkSumAddress = (size_t)&(OptHeader64->ImageChecksum);
        headerCheckSum = OptHeader64->ImageChecksum;
    }

    // Do the checksum calculation in any case, the caller might want to know
    // of it. Like our mapper that uses it for hashtables :-)
    calculatedCheckSum = __CalculateChecksum(
            buffer,
            length,
            checkSumAddress - ((size_t) buffer)
    );
    *checksumOut = calculatedCheckSum;

    // However we only do image validation if the header actually provides
    // us a checksum, otherwise what would the point be.
    if (headerCheckSum != 0 && calculatedCheckSum != headerCheckSum) {
        ERROR("Invalid checksum of file (Header 0x%x, Calculated 0x%x)",
                headerCheckSum, calculatedCheckSum);
        return OS_EUNKNOWN;
    }
    return OS_EOK;
}

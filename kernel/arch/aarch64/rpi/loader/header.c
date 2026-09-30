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

struct __RpiHeader {
    uint8_t  signature[8];
    uint32_t version;
    uint32_t headerSize;
    uint64_t loaderLength;
    uint64_t totalLength;
    uint64_t reserved1;
    uint64_t reserved2;
};

static const unsigned char g_magic[8] = {'V', 'A', 'L', 'I', 'R', 'P', 'I', 0};

enum RpiBootStatus
__ValidateHeader(
    const unsigned char* header,
    size_t               headerLength,
    uint64_t             loaderLength)
{
    struct __RpiHeader* rpi = (struct __RpiHeader*)header;

    if (header == NULL) {
        return RpiBootInvalidPayload;
    }
    
    if (headerLength != RPI_PAYLOAD_HEADER_SIZE) {
        return RpiBootInvalidPayload;
    }
    
    if (loaderLength < RPI_PAYLOAD_HEADER_SIZE || (loaderLength & 4095)) {
        return RpiBootInvalidPayload;
    }

    // Verify the signature
    for (unsigned int i = 0; i < sizeof(g_magic); ++i) {
        if (rpi->signature[i] != g_magic[i]) {
            return RpiBootInvalidPayload;
        }
    }
    
    if (SWAP32(rpi->version) != 1) {
        return RpiBootInvalidPayload;
    } else if (SWAP32(rpi->headerSize) != RPI_PAYLOAD_HEADER_SIZE) {
        return RpiBootInvalidPayload;
    } else if (SWAP64(rpi->loaderLength) != loaderLength) {
        return RpiBootInvalidPayload;
    } else if (SWAP64(rpi->totalLength) != loaderLength) {
        return RpiBootInvalidPayload;
    } else if (SWAP64(rpi->reserved1) || SWAP64(rpi->reserved2)) {
        return RpiBootInvalidPayload;
    }
    
    return RpiBootOk;
}

uint64_t
__SwapHeaderValue64(
    const unsigned char* header,
    size_t               offset)
{
    uint64_t value = *((uint64_t*)(header + offset));
    return SWAP64(value);
}

enum RpiBootStatus
RpiParseHeader(
    const unsigned char*         header,
    size_t                       headerLength,
    uint64_t                     loaderLength,
    uint64_t                     availableLength,
    struct RpiKernelInformation* kernelInfo)
{
    enum RpiBootStatus status;
    uint64_t           length;
    uint64_t           total;

    status = __ValidateHeader(header, headerLength, loaderLength);
    if (status != RpiBootOk) {
        return RpiBootInvalidPayload;
    }
    
    length = __SwapHeaderValue64(header, 32);
    total = __SwapHeaderValue64(header, 40);
    
    // Subtraction makes a forged total incapable of wrapping the range check.
    if (!length || length > RPI_PAYLOAD_MAX_SIZE || total > availableLength ||
        total < loaderLength || total - loaderLength != length) {
        return RpiBootInvalidPayload;
    }

    // Update the kernel information structure
    kernelInfo->Offset = loaderLength;
    kernelInfo->Length = length;
    kernelInfo->ImageLength = total;
    return RpiBootOk;
}

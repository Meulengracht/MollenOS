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

// Wire layout used only for field offsets; decoding always reads individual bytes.
struct __RpiHeader {
    uint8_t  signature[8];
    uint32_t version;
    uint32_t headerSize;
    uint64_t loaderLength;
    uint64_t kernelOffset;
    uint64_t kernelLength;
    uint64_t totalLength;
    uint64_t reserved1;
    uint64_t reserved2;
};

static const unsigned char g_magic[8] = {'V', 'A', 'L', 'I', 'R', 'P', 'I', 0};

static uint64_t
__ReadHeaderValue(
    const unsigned char* header,
    size_t               offset,
    unsigned int         length)
{
    uint64_t value = 0;
    
    // The packager and linker emit a little-endian trailer. It is independent
    // of the big-endian DTB, and must not be byte-swapped on little-endian ARM.
    // Byte reads also avoid assuming the caller supplied an aligned header.
    for (unsigned int i = 0; i < length; i++) {
        value |= (uint64_t)header[offset + i] << (8 * i);
    }
    
    return value;
}

enum RpiBootStatus
__ValidateHeader(
    const unsigned char* header,
    size_t               headerLength,
    uint64_t             loaderLength)
{
    uint64_t value;

    if (header == NULL) {
        return RpiBootInvalidPayload;
    }
    
    if (headerLength != RPI_PAYLOAD_HEADER_SIZE) {
        return RpiBootInvalidPayload;
    }
    
    // The fixed trailer ends at the page-aligned boundary of the wrapper file.
    if (loaderLength < RPI_PAYLOAD_HEADER_SIZE || (loaderLength & RPI_PAGE_MASK)) {
        return RpiBootInvalidPayload;
    }

    // Verify the signature
    for (unsigned int i = 0; i < sizeof(g_magic); ++i) {
        if (header[i] != g_magic[i]) {
            return RpiBootInvalidPayload;
        }
    }
    
    // Decode before deciding; an unsupported version can change the field layout.
    value = __ReadHeaderValue(header, offsetof(struct __RpiHeader, version), 4);
    if (value != RPI_PAYLOAD_VERSION) {
        return RpiBootInvalidPayload;
    }

    // This decoder accepts only the fixed trailer extent emitted by our packager.
    value = __ReadHeaderValue(header, offsetof(struct __RpiHeader, headerSize), 4);
    if (value != RPI_PAYLOAD_HEADER_SIZE) {
        return RpiBootInvalidPayload;
    }

    // The recorded wrapper extent must match the linker-provided boundary.
    value = __ReadHeaderValue(header, offsetof(struct __RpiHeader, loaderLength), 8);
    if (value != loaderLength) {
        return RpiBootInvalidPayload;
    }

    // The kernel file must start directly after the complete wrapper.
    value = __ReadHeaderValue(header, offsetof(struct __RpiHeader, kernelOffset), 8);
    if (value != loaderLength) {
        return RpiBootInvalidPayload;
    }

    // Nonzero reserved fields could request behavior this version does not support.
    value = __ReadHeaderValue(header, offsetof(struct __RpiHeader, reserved1), 8);
    if (value) {
        return RpiBootInvalidPayload;
    }
    value = __ReadHeaderValue(header, offsetof(struct __RpiHeader, reserved2), 8);
    if (value) {
        return RpiBootInvalidPayload;
    }
    
    return RpiBootOk;
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
    
    length = __ReadHeaderValue(header, offsetof(struct __RpiHeader, kernelLength), 8);
    total = __ReadHeaderValue(header, offsetof(struct __RpiHeader, totalLength), 8);
    
    // The kernel payload must be nonempty and within the trusted-file size budget.
    if (!length || length > RPI_PAYLOAD_MAX_SIZE) {
        return RpiBootInvalidPayload;
    }

    // Check both total bounds before subtracting the wrapper's length.
    if (total > availableLength || total < loaderLength) {
        return RpiBootInvalidPayload;
    }

    // The appended kernel must account for every byte after the wrapper.
    if (total - loaderLength != length) {
        return RpiBootInvalidPayload;
    }

    // Update the kernel information structure
    kernelInfo->Offset = loaderLength;
    kernelInfo->Length = length;
    kernelInfo->ImageLength = total;
    return RpiBootOk;
}

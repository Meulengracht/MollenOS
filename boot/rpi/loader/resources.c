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
#include <string.h>

// Wire layout for field offsets only; never dereference it on MMU-off hardware.
struct __BundleHeader {
    unsigned char Signature[8];
    uint32_t      Version;
    uint32_t      HeaderSize;
    uint64_t      TotalLength;
    uint64_t      PhoenixOffset;
    uint64_t      PhoenixLength;
    uint64_t      RamdiskOffset;
    uint64_t      RamdiskLength;
    uint64_t      Reserved;
};

_Static_assert(
    sizeof(struct __BundleHeader) == RPI_BUNDLE_HEADER_SIZE,
    "bundle field offsets must match the packager");

static uint64_t
__ReadLe64(
    _In_ const unsigned char* bytes)
{
    uint64_t value = 0;

    for (unsigned int i = 0; i < 8; i++) {
        value |= (uint64_t)bytes[i] << (i * 8);
    }
    return value;
}

static int
__BundleHeaderValid(
    _In_ const unsigned char* bundle,
    _In_ uint64_t             length)
{
    int      signatureComparison;
    uint64_t value;

    // Identify our bundle before interpreting any version-specific fields.
    signatureComparison = memcmp(bundle, "VALIBND", sizeof("VALIBND"));
    if (signatureComparison) {
        return 0;
    }

    // This word packs the 32-bit version below the 32-bit header length.
    value = __ReadLe64(bundle + offsetof(struct __BundleHeader, Version));
    if (value != ((uint64_t)RPI_BUNDLE_HEADER_SIZE << 32 | RPI_BUNDLE_VERSION)) {
        return 0;
    }

    // The transport extent and recorded length must agree, including padding.
    value = __ReadLe64(bundle + offsetof(struct __BundleHeader, TotalLength));
    if (value != length) {
        return 0;
    }

    // Reserved bits cannot request unimplemented behavior in this format version.
    value = __ReadLe64(bundle + offsetof(struct __BundleHeader, Reserved));
    if (value) {
        return 0;
    }
    return 1;
}

enum RpiBootStatus
RpiLoadResources(
    _In_ struct RpiBootContext* context)
{
    const unsigned char* bundle;
    uint64_t             phoenixOffset;
    uint64_t             phoenixLength;
    uint64_t             ramdiskOffset;
    uint64_t             ramdiskLength;
    enum RpiBootStatus   status;

    context->BootInformation.Phoenix = (struct VBootModule){0};
    context->BootInformation.Ramdisk = (struct VBootRamdisk){0};
    
    if (!context->ExternalPayloadLength) {
        return RpiBootOk;
    }

    // Platform preparation has established accessibility and exclusive ownership
    // of this physical range. Only the transport's contents remain untrusted.
    if (context->ExternalPayloadLength < RPI_BUNDLE_HEADER_SIZE) {
        return RpiBootInvalidPayload;
    }
    
    bundle = (const unsigned char*)(uintptr_t)context->ExternalPayloadBase;
    if (!__BundleHeaderValid(bundle, context->ExternalPayloadLength)) {
        return RpiBootInvalidPayload;
    }
    
    phoenixOffset = __ReadLe64(bundle + offsetof(struct __BundleHeader, PhoenixOffset));
    phoenixLength = __ReadLe64(bundle + offsetof(struct __BundleHeader, PhoenixLength));
    ramdiskOffset = __ReadLe64(bundle + offsetof(struct __BundleHeader, RamdiskOffset));
    ramdiskLength = __ReadLe64(bundle + offsetof(struct __BundleHeader, RamdiskLength));

    // Phoenix follows the fixed header and must contain a bounded, nonempty PE.
    if (phoenixOffset != RPI_BUNDLE_HEADER_SIZE || !phoenixLength ||
        phoenixLength > RPI_PAYLOAD_MAX_SIZE) {
        return RpiBootInvalidPayload;
    }

    // Header bounds above make this subtraction safe; the whole PE must exist.
    if (phoenixLength > context->ExternalPayloadLength - phoenixOffset) {
        return RpiBootInvalidPayload;
    }

    // Page padding separates the PE from the ramdisk; the two cannot overlap.
    if (ramdiskOffset < phoenixOffset + phoenixLength || (ramdiskOffset & RPI_PAGE_MASK)) {
        return RpiBootInvalidPayload;
    }

    // Check the ramdisk offset before subtracting it, and fit its VBoot length field.
    if (ramdiskOffset > context->ExternalPayloadLength || !ramdiskLength ||
        ramdiskLength > UINT32_MAX) {
        return RpiBootInvalidPayload;
    }

    // The exact remaining bytes are the ramdisk, not padding or a truncated load.
    if (ramdiskLength != context->ExternalPayloadLength - ramdiskOffset) {
        return RpiBootInvalidPayload;
    }
    
    status = RpiLoadPhoenix(context, bundle + phoenixOffset, phoenixLength);
    if (status != RpiBootOk) {
        return status;
    }
    
    context->BootInformation.Ramdisk.Data = context->ExternalPayloadBase + ramdiskOffset;
    context->BootInformation.Ramdisk.Length = (uint32_t)ramdiskLength;
    return RpiBootOk;
}

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

#define RPI_BUNDLE_HEADER_SIZE 64u

static uint64_t
__ReadLe64(
    const unsigned char* bytes)
{
    uint64_t value = 0;

    for (unsigned int i = 0; i < 8; i++) {
        value |= (uint64_t)bytes[i] << (i * 8);
    }
    return value;
}

enum RpiBootStatus
RpiLoadResources(
    struct RpiBootContext* context)
{
    const unsigned char* bundle;
    uint64_t phoenixOffset;
    uint64_t phoenixLength;
    uint64_t ramdiskOffset;
    uint64_t ramdiskLength;
    enum RpiBootStatus status;

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
    if (memcmp(bundle, "VALIBND\0", 8) ||
        __ReadLe64(bundle + 8) != ((uint64_t)RPI_BUNDLE_HEADER_SIZE << 32 | 1) ||
        __ReadLe64(bundle + 16) != context->ExternalPayloadLength ||
        __ReadLe64(bundle + 56) != 0) {
        return RpiBootInvalidPayload;
    }
    phoenixOffset = __ReadLe64(bundle + 24);
    phoenixLength = __ReadLe64(bundle + 32);
    ramdiskOffset = __ReadLe64(bundle + 40);
    ramdiskLength = __ReadLe64(bundle + 48);

    // The canonical layout is header, PE, page padding, ramdisk. Exact lengths
    // distinguish the ramdisk from padding and reject truncated firmware loads.
    if (phoenixOffset != RPI_BUNDLE_HEADER_SIZE || !phoenixLength ||
        phoenixLength > RPI_PAYLOAD_MAX_SIZE ||
        phoenixLength > context->ExternalPayloadLength - phoenixOffset ||
        ramdiskOffset < phoenixOffset + phoenixLength || (ramdiskOffset & 4095) ||
        ramdiskOffset > context->ExternalPayloadLength || !ramdiskLength ||
        ramdiskLength > UINT32_MAX ||
        ramdiskLength != context->ExternalPayloadLength - ramdiskOffset) {
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

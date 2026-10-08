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
 * Device Manager
 * - Implementation of the device manager in the operating system.
 *   Keeps track of devices, their loaded drivers and bus management.
 */

#include <discover.h>
#include <ddk/platformdevice.h>
#include <string.h>

/**
 * @brief Matches a platform device against the driver's firmware compatibles.
 *
 * The device list is ordered by firmware preference. Its position is therefore
 * the match score: the first compatible in the device list that the driver
 * supports is the strongest match.
 */
static unsigned int
__MatchPlatformDevice(
    _In_ const struct DriverConfiguration*  configuration,
    _In_ const struct DriverIdentification* identification)
{
    struct DriverCompatible* compatible;
    const char*              entry;
    const char*              end;
    size_t                   offset = 0;
    size_t                   length;
    unsigned int             rank = 1;
    unsigned int             best = 0;

    // Platform descriptors have a fixed-size compatible array. Reject absent,
    // empty, or oversized data before reading from that array.
    if (identification->Compatibles == NULL || !identification->CompatibleLength ||
        identification->CompatibleLength > PLATFORM_DEVICE_MAX_COMPATIBLES) {
        return 0;
    }

    while (offset < identification->CompatibleLength) {
        entry = identification->Compatibles + offset;

        // Bound the search to the remaining descriptor bytes, and reject an
        // unterminated or empty item so malformed data can never match a name.
        end = memchr(entry, 0, identification->CompatibleLength - offset);
        if (end == NULL || end == entry) {
            return 0;
        }
        length = (size_t)(end - entry);

        foreach (i, &configuration->Compatibles) {
            compatible = i->value;

            // Compare lengths first for an exact match without reading past
            // either string; preserve the earliest firmware-preferred match.
            if (!best && strlen(compatible->Name) == length &&
                !memcmp(compatible->Name, entry, length)) {
                best = rank;
            }
        }
        offset += length + 1;
        rank++;
    }
    return best;
}

static unsigned int
__MatchBusDevice(
    _In_ const struct DriverConfiguration*  configuration,
    _In_ const struct DriverIdentification* identification)
{
    struct DriverVendor*  vendor;
    struct DriverProduct* product;

    // Compatible strings identify platform devices. Do not let such a
    // configuration bind a different device through unset numeric fields.
    if (configuration->Compatibles.count) {
        return 0;
    }

    // Vendor ID zero means no vendor identity was supplied, so it cannot
    // establish a vendor/product match. Products are scoped to their vendor.
    if (identification->VendorId) {
        foreach (i, &configuration->Vendors) {
            vendor = i->value;
            if (vendor->Id != identification->VendorId) {
                continue;
            }
            foreach (j, &vendor->Products) {
                product = j->value;
                if (product->Id == identification->ProductId) {
                    return 1;
                }
            }
        }
    }

    // Require at least one class component to be specified. Otherwise default
    // zero values on both sides could incorrectly make unrelated devices match.
    if (!identification->Class && !identification->Subclass) {
        return 0;
    }

    // A class match is less specific than a vendor/product match. Give it a
    // higher score so driver order cannot make it replace an exact match.
    if (identification->Class == configuration->Class &&
        identification->Subclass == configuration->Subclass) {
        return 2;
    }
    return 0;
}

unsigned int
DmDriverMatchScore(
    _In_ const struct DriverConfiguration*  configuration,
    _In_ const struct DriverIdentification* identification)
{
    // IsPlatform means the device was enumerated from firmware as a
    // PlatformDevice_t. Its ordered compatible strings are its identity; the
    // numeric fields are not a fallback because they may just be zero defaults.
    if (identification->IsPlatform) {
        return __MatchPlatformDevice(configuration, identification);
    }

    return __MatchBusDevice(configuration, identification);
}

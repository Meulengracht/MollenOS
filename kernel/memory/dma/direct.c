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
 */

#include "private.h"

static bool
__ExtentFits(
    _In_ uint64_t base,
    _In_ uint64_t length,
    _In_ uint64_t limit)
{
    // Use subtraction to avoid overflow. base + length would wrap when the
    // last byte is UINT64_MAX, even though that range is valid.
    return length != 0 && base <= limit && length - 1 <= limit - base;
}

oserr_t
DMADirectValidateRanges(
    _In_ const struct SHMDeviceRange* ranges,
    _In_ uint32_t                     count)
{
    // Device ranges must not overlap. Physical ranges may overlap when the
    // same physical bytes can be reached through different device addresses.
    for (uint32_t i = 0; i < count; i++) {
        if (!__ExtentFits(ranges[i].PhysicalBase, ranges[i].Length, UINT64_MAX) ||
            !__ExtentFits(ranges[i].DeviceBase, ranges[i].Length, UINT64_MAX)) {
            return OS_EINVALPARAMS;
        }

        for (uint32_t j = 0; j < i; j++) {
            uint64_t first = ranges[i].DeviceBase;
            uint64_t second = ranges[j].DeviceBase;
            if (first <= second ? second - first < ranges[i].Length :
                                  first - second < ranges[j].Length) {
                return OS_EINVALPARAMS;
            }
        }
    }
    return OS_EOK;
}

/**
 * @brief Find how many requested bytes one device address range can cover.
 *
 * Starting at physicalBase, choose the range that covers the most bytes
 * within the device address limit. If several ranges cover the same amount,
 * choose the lowest device address. Input order does not affect the result.
 * Whole-buffer translation and segment building both use this rule.
 *
 * @param length Nonzero, and physicalBase + length - 1 must not wrap.
 * @return Number of consecutive bytes reachable from physicalBase, or 0 if the
 *         device cannot reach the first byte. addressOut is set only when the
 *         result is nonzero.
 */
static uint64_t
__TranslatePrefix(
    _In_  const struct DMAAddressSpace* space,
    _In_  uint64_t                      addressLimit,
    _In_  uint64_t                      physicalBase,
    _In_  uint64_t                      length,
    _Out_ uint64_t*                     addressOut)
{
    uint64_t deviceLimit = addressLimit;
    uint64_t best = 0;
    uint64_t bestAddress = 0;

    for (uint32_t i = 0; i < space->Count; i++) {
        const struct SHMDeviceRange* range = &space->Ranges[i];
        uint64_t                     offset;
        uint64_t                     address;
        uint64_t                     last;
        uint64_t                     covered;

        if (physicalBase < range->PhysicalBase) {
            continue;
        }

        offset = physicalBase - range->PhysicalBase;
        if (offset >= range->Length) {
            continue;
        }

        // The range was checked at creation, so adding an offset inside it
        // cannot overflow. The first byte must also fit the device's limit.
        address = range->DeviceBase + offset;
        if (address > deviceLimit) {
            continue;
        }

        // Use the offset of the last byte rather than a byte count. The range
        // from zero through UINT64_MAX has 2^64 bytes, which does not fit in uint64_t.
        last = length - 1;
        if (last > range->Length - 1 - offset) {
            last = range->Length - 1 - offset;
        }
        if (last > deviceLimit - address) {
            last = deviceLimit - address;
        }

        // last + 1 cannot wrap because last is at most length - 1.
        covered = last + 1;
        if (covered > best || (covered == best && address < bestAddress)) {
            best = covered;
            bestAddress = address;
        }
    }

    if (best != 0) {
        *addressOut = bestAddress;
    }
    return best;
}

oserr_t
DMADirectTranslate(
    _In_  const struct DMAAddressSpace* space,
    _In_  uint64_t                      addressLimit,
    _In_  uint64_t                      physicalBase,
    _In_  uint64_t                      length,
    _Out_ uint64_t*                     deviceAddressOut)
{
    uint64_t address;
    uint64_t covered;

    // Check the range before subtracting addresses or lengths.
    if (space == NULL || deviceAddressOut == NULL) {
        return OS_EINVALPARAMS;
    }

    if (!__ExtentFits(physicalBase, length, UINT64_MAX)) {
        return OS_EINVALPARAMS;
    }

    covered = __TranslatePrefix(
        space,
        addressLimit,
        physicalBase,
        length,
        &address
    );

    // One device range must cover the whole buffer. __TranslatePrefix chooses
    // the lowest device address among the ranges that do.
    if (covered != length) {
        return OS_ENOENT;
    }
    *deviceAddressOut = address;
    return OS_EOK;
}

/**
 * @brief Check whether a piece can extend the previous device segment.
 *
 * Consecutive device addresses form one segment even when their physical
 * bytes came from separate ranges. The combined length must not overflow.
 */
static bool
__ContinuesSegment(
    _In_ const struct DMASegment* previous,
    _In_ uint64_t                 address,
    _In_ uint64_t                 length)
{
    if (address < previous->Address || address - previous->Address != previous->Length) {
        return false;
    }
    return length <= UINT64_MAX - previous->Length;
}

static bool
__SegmentMeetsRequirements(
    _In_ const struct DMASegment*         segment,
    _In_ const struct DMAMapRequirements* requirements)
{
    if (requirements == NULL) {
        return true;
    }
    if (requirements->AddressAlignment != 0 &&
        (segment->Address & (requirements->AddressAlignment - 1)) != 0) {
        return false;
    }
    return requirements->LengthAlignment == 0 ||
        (segment->Length & (requirements->LengthAlignment - 1)) == 0;
}

/**
 * @brief Convert physical ranges into device segments, counting or writing them.
 *
 * Both calls use the same loop so they split and combine the same byte ranges.
 *
 * @param segmentsOut NULL to count only. Otherwise it must hold every segment.
 */
oserr_t
DMADirectWalkSegments(
    _In_  const struct DMAAddressSpace*    space,
    _In_  uint64_t                         addressLimit,
    _In_  const SHMSG_t*                   extents,
    _In_  int                              extentCount,
    _In_  const struct DMAMapRequirements* requirements,
    _Out_ struct DMASegment*               segmentsOut,
    _Out_ uint32_t*                        countOut)
{
    struct DMASegment last = { 0 };
    uint32_t          count = 0;

    for (int i = 0; i < extentCount; i++) {
        uint64_t physical = extents[i].Address;
        uint64_t remaining = extents[i].Length;

        // The range must be nonempty and its last address must not overflow.
        if (!__ExtentFits(physical, remaining, UINT64_MAX)) {
            return OS_EINVALPARAMS;
        }

        // Take as many bytes as one device range can reach, up to the end of
        // that range or the device address limit.
        while (remaining != 0) {
            uint64_t address;
            uint64_t covered = __TranslatePrefix(space, addressLimit, physical, remaining, &address);
            
            if (covered == 0) {
                return OS_ENOENT;
            }

            // Keep the current segment here even when only counting.
            // Both calls must combine adjacent pieces the same way.
            if (count != 0 && __ContinuesSegment(&last, address, covered)) {
                last.Length += covered;
            } else {
                // Check alignment after adjacent pieces have been combined.
                // The final segment is checked after the loop.
                if (count != 0 && !__SegmentMeetsRequirements(&last, requirements)) {
                    return OS_ENOTSUPPORTED;
                }
                
                if (count == UINT32_MAX) {
                    return OS_ENOTSUPPORTED;
                }
                
                last = (struct DMASegment){
                    address,
                    covered
                };
                count++;
            }
            
            if (segmentsOut != NULL) {
                segmentsOut[count - 1] = last;
            }

            // After the byte at UINT64_MAX, physical wraps to zero.
            // remaining is then zero, so the loop ends.
            physical += covered;
            remaining -= covered;
        }
    }

    if (!__SegmentMeetsRequirements(&last, requirements)) {
        return OS_ENOTSUPPORTED;
    }

    *countOut = count;
    return OS_EOK;
}

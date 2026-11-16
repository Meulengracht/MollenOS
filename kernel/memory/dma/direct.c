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
    // Compare with subtraction. Adding base and length would wrap around to
    // zero when the range ends at UINT64_MAX, and that range is still valid.
    return length != 0 && base <= limit && length - 1 <= limit - base;
}

oserr_t
DMAAddressRangesValidate(
    _In_ const struct SHMDeviceRange* ranges,
    _In_ uint32_t                     count)
{
    if (ranges == NULL || count == 0) {
        return OS_EINVALPARAMS;
    }
    
    if (count > DMA_MAX_RANGES) {
        return OS_ENOTSUPPORTED;
    }

    // Device ranges must not overlap, because one device address would then
    // lead to two places. Physical ranges may overlap. That only means the
    // same memory can be reached at more than one device address.
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
 * Look at every range that contains physicalBase. Pick the one that reaches
 * the most bytes without going past the device address limit. If two ranges
 * reach the same amount, pick the one with the lowest device address. The
 * order of the ranges does not change the result.
 *
 * @param length Number of bytes wanted. Must not be zero, and the last byte
 *               (physicalBase plus length minus one) must not wrap around.
 * @param addressOut Receives the device address of physicalBase.
 * @return Number of bytes in a row that the device can reach from
 *         physicalBase, or 0 if it cannot reach the first byte. addressOut is
 *         only written when the result is nonzero.
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

        // The range was checked when it was created, so adding an offset that
        // lies inside it cannot overflow. Skip the range if its first byte is
        // above the device address limit.
        address = range->DeviceBase + offset;
        if (address > deviceLimit) {
            continue;
        }

        // Work with the offset of the last byte instead of a byte count. A
        // range that covers every address from 0 to UINT64_MAX holds 2^64
        // bytes, and that count does not fit in uint64_t.
        last = length - 1;
        if (last > range->Length - 1 - offset) {
            last = range->Length - 1 - offset;
        }
        if (last > deviceLimit - address) {
            last = deviceLimit - address;
        }

        // Adding one cannot wrap, because last is never larger than the
        // requested length minus one.
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

/**
 * @brief Check whether a piece can extend the previous device segment.
 *
 * A piece extends the segment when it starts at the device address right
 * after the segment's last byte. This holds even if their physical bytes came
 * from separate ranges. The combined length must also fit in 64 bits.
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

/**
 * @brief Check a finished segment against the optional alignment requirements.
 *
 * An alignment of 0 means the segment has no alignment requirement for that
 * value. Both alignments are powers of two when set.
 */
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
 * The counting call and the filling call run this same code, so they split and
 * merge the same byte ranges and report the same count.
 *
 * @param segmentsOut NULL to count only. Otherwise it must have room for every
 *                    segment, using the count from an earlier counting call.
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

        // Reject an empty range, or one that would run past the last address.
        if (!__ExtentFits(physical, remaining, UINT64_MAX)) {
            return OS_EINVALPARAMS;
        }

        // Translate in pieces. Each piece covers as many bytes as one device
        // range can reach, ending at the end of that range or at the device
        // address limit.
        while (remaining != 0) {
            uint64_t address;
            uint64_t covered = __TranslatePrefix(space, addressLimit, physical, remaining, &address);
            
            if (covered == 0) {
                return OS_ENOENT;
            }

            // Track the last segment even when only counting. That way both
            // calls merge adjacent pieces in exactly the same way.
            if (count != 0 && __ContinuesSegment(&last, address, covered)) {
                last.Length += covered;
            } else {
                // Alignment applies to the merged segment, so check the
                // previous segment only once a new one starts. The final
                // segment is checked after the loop.
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

            // If this piece ended at the last possible address, physical wraps
            // to zero here. Remaining is zero in that case, so the loop stops
            // and the wrapped value is never used.
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

/**
 * Copyright 2023, Philip Meulengracht
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

#ifndef __BITMAP_H__
#define __BITMAP_H__

#include <stdint.h>
#include <string.h>

typedef struct bitmap {
    int       total;
    int       clear;
    uint32_t* data;
} bitmap_t;

#define __BITMAP_BSIZE (sizeof(uint32_t) * 8)
#define __BITMAP_BMASK 0xFFFFFFFF
#define BITMAP_SIZE(count) (((count) + __BITMAP_BSIZE - 1) / __BITMAP_BSIZE)

static inline void
bitmap_construct(
        _In_ bitmap_t* bitmap,
        _In_ int       count,
        _In_ void*     data)
{
    bitmap->total = count;
    bitmap->clear = count;
    bitmap->data = data;
    memset(data, 0, BITMAP_SIZE(count) * sizeof(uint32_t));
}

static inline int
bitmap_bits_clear_count(
        _In_ bitmap_t* bitmap)
{
    return bitmap->clear;
}

static int
bitmap_set(
        _In_ bitmap_t* bitmap,
        _In_ int       index,
        _In_ int       count)
{
    int block  = index / (int)__BITMAP_BSIZE;
    int blockIndex = index % (int)__BITMAP_BSIZE;
    int bitsLeft = count;
    int bitsSet = 0;

    for (int i = block; BITMAP_SIZE(bitmap->total) && (bitsLeft > 0); i++) {
        // optimization case for larger sets/clears
        if (bitmap->data[i] == 0 && blockIndex == 0 && bitsLeft >= __BITMAP_BSIZE) {
            bitmap->data[i] = __BITMAP_BMASK;
            bitsSet += __BITMAP_BSIZE;
            bitsLeft -= __BITMAP_BSIZE;
            continue;
        }

        for (int j = blockIndex; (j < __BITMAP_BSIZE) && (bitsLeft > 0); j++, bitsLeft--) {
            if (!(bitmap->data[i] & (1U << j))) {
                bitmap->data[i] |= (1U << j);
                bitsSet++;
            }
        }
        blockIndex = 0;
    }
    bitmap->clear -= bitsSet;
    return bitsSet;
}

static int
bitmap_clear(
        _In_ bitmap_t* bitmap,
        _In_ int       index,
        _In_ int       count)
{
    int block = index / (int)__BITMAP_BSIZE;
    int blockIndex = index % (int)__BITMAP_BSIZE;
    int bitsLeft = count;
    int bitsCleared = 0;

    for (int i = block; BITMAP_SIZE(bitmap->total) && (bitsLeft > 0); i++) {
        // optimization case for larger sets/clears
        if (bitmap->data[i] == __BITMAP_BMASK && blockIndex == 0 && bitsLeft >= __BITMAP_BSIZE) {
            bitmap->data[i] = 0;
            bitsCleared += __BITMAP_BSIZE;
            bitsLeft -= __BITMAP_BSIZE;
            continue;
        }

        for (int j = blockIndex; (j < __BITMAP_BSIZE) && (bitsLeft > 0); j++, bitsLeft--) {
            if (bitmap->data[i] & (1U << j)) {
                bitmap->data[i] &= ~(1U << j);
                bitsCleared++;
            }
        }
        blockIndex = 0;
    }
    bitmap->clear += bitsCleared;
    return bitsCleared;
}

static bool
bitmap_bits_clear(
        _In_ bitmap_t*  bitmap,
        _In_ int        index,
        _In_ int        count)
{
    int block = index / (int)__BITMAP_BSIZE;
    int blockIndex = index % (int)__BITMAP_BSIZE;
    int bitsLeft = count;

    for (int i = block; BITMAP_SIZE(bitmap->total) && (bitsLeft > 0); i++) {
        for (int j = blockIndex; j < __BITMAP_BSIZE && bitsLeft > 0; j++, bitsLeft--) {
            if (bitmap->data[i] & (1U << j)) {
                return false;
            }
        }
        blockIndex = 0;
    }
    return true;
}

static bool
bitmap_bits_set(
        _In_ bitmap_t*  bitmap,
        _In_ int        index,
        _In_ int        count)
{
    int block = index / (int)__BITMAP_BSIZE;
    int blockIndex = index % (int)__BITMAP_BSIZE;
    int bitsLeft = count;

    for (int i = block; BITMAP_SIZE(bitmap->total) && (bitsLeft > 0); i++) {
        for (int j = blockIndex; j < __BITMAP_BSIZE && bitsLeft > 0; j++, bitsLeft--) {
            if (!(bitmap->data[i] & (1U << j))) {
                return false;
            }
        }
        blockIndex = 0;
    }
    return true;
}

#endif //!__BITMAP_H__

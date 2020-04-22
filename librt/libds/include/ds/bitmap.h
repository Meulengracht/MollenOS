/* MollenOS
 *
 * Copyright 2017, Philip Meulengracht
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
 * Generic Bitmap Implementation
 */

#ifndef __BITMAP_H__
#define __BITMAP_H__

#include <ds/dsdefs.h>

typedef struct {
    int     Cleanup;
    size_t  SizeInBytes;
    size_t  BitCount;
	size_t* Data;
} Bitmap_t;

/* BitmapCreate
 * Creates a bitmap of the given size in bytes, the actual available
 * member count will then be Size * sizeof(byte). This automatically
 * allocates neccessary resources */
DSDECL(Bitmap_t*,
BitmapCreate(
    _In_ size_t Size));

/* BitmapConstruct
 * Creates a bitmap of the given size in bytes, the actual available
 * member count will then be Size * sizeof(byte). This uses user-provided
 * resources, and won't be cleaned up. */
DSDECL(OsStatus_t,
BitmapConstruct(
    _In_ Bitmap_t* Bitmap,
    _In_ size_t*   Data,
    _In_ size_t    Size));

/* BitmapDestroy
 * Cleans up any resources allocated by the Create/Construct. */
DSDECL(OsStatus_t,
BitmapDestroy(
    _In_ Bitmap_t* Bitmap));

/* BitmapSetBits
 * Flips all bits to 1 at the given index, and for <Count> bits. Returns the 
 * actual number of bits set in this iteration. */
DSDECL(int,
BitmapSetBits(
    _In_    Bitmap_t* Bitmap,
    _InOut_ int*      SearchIndex,
    _In_    int       Index,
    _In_    int       Count));

/* BitmapClearBits
 * Clears all bits from the given index, and for <Count> bits. Returns the number
 * of bits cleared in this iteration. */
DSDECL(int,
BitmapClearBits(
    _In_    Bitmap_t* Bitmap,
    _InOut_ int*      SearchIndex,
    _In_    int       Index,
    _In_    int       Count));

/* BitmapAreBitsSet
 * If all bits are set from the given index, and for <Count> bits, then this
 * function returns 1. Otherwise 0. */
DSDECL(int,
BitmapAreBitsSet(
    _In_ Bitmap_t* Bitmap,
    _In_ int       Index,
    _In_ int       Count));

/* BitmapAreBitsClear
 * If all bits are cleared from the given index, and for <Count> bits, then this
 * function returns 1. Otherwise 0. */
DSDECL(int,
BitmapAreBitsClear(
    _In_ Bitmap_t* Bitmap,
    _In_ int       Index,
    _In_ int       Count));

/* BitmapFindBits
 * Locates the requested number of consequtive free bits.
 * Returns the index of the first free bit. Returns -1 on no free. */
DSDECL(int,
BitmapFindBits(
    _In_    Bitmap_t* Bitmap,
    _InOut_ int*      SearchIndex,
    _In_    int       Count));

#endif //!__BITMAP_H__

/* MollenOS
 *
 * Copyright 2011 - 2017, Philip Meulengracht
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
 * MollenOS MCore - Generic Bitmap Implementation
 */

#include <os/osdefs.h>
#include <ds/bitmap.h>
#include <string.h>
#include <stddef.h>
#include <assert.h>

/* BitmapCreate
 * Creates a bitmap of the given size in bytes, the actual available
 * member count will then be Size * sizeof(byte). This automatically
 * allocates neccessary resources */
Bitmap_t*
BitmapCreate(
    _In_ size_t     Size)
{
    // Variables
    Bitmap_t *Bitmap = NULL;
    uintptr_t *Data = NULL;
    assert(Size > 0);

    // Allocate a new bitmap and associated buffer
    Bitmap  = (Bitmap_t*)dsalloc(sizeof(Bitmap_t));
    Data    = (uintptr_t*)dsalloc(Size);

    // Construct the bitmap
    if (BitmapConstruct(Bitmap, Data, Size) != OsSuccess) {
        dsfree((void*)Data);
        dsfree((void*)Bitmap);
        return NULL;
    }

    // Flip cleanup
    Bitmap->Cleanup = 1;
    return Bitmap;
}

/* BitmapConstruct
 * Creates a bitmap of the given size in bytes, the actual available
 * member count will then be Size * sizeof(byte). This uses user-provided
 * resources, and won't be cleaned up. */
OsStatus_t
BitmapConstruct(
    _In_ Bitmap_t*  Bitmap,
    _In_ uintptr_t* Data,
    _In_ size_t     Size)
{
    assert(Bitmap != NULL);
    assert(Data != NULL);
    assert(Size > 0);

    // Fill in data
    memset(Data, 0, Size);
    Bitmap->Data        = Data;
    Bitmap->SizeInBytes = Size;
    Bitmap->Cleanup     = 0;
    Bitmap->Capacity    = (Size * 8);
    return OsSuccess;
}

/* BitmapDestroy
 * Cleans up any resources allocated by the Create/Construct. */
OsStatus_t
BitmapDestroy(
    _In_ Bitmap_t*  Bitmap)
{
    assert(Bitmap != NULL);

    // Should we cleanup bitmap?
    if (Bitmap->Cleanup != 0) {
        dsfree((void*)Bitmap->Data);
        dsfree((void*)Bitmap);
    }
    return OsSuccess;
}

/* BitmapSetBits
 * Flips all bits to 1 at the given index, and for <Count> bits. Returns the 
 * actual number of bits set in this iteration. */
int
BitmapSetBits(
    _In_ Bitmap_t*  Bitmap,
    _In_ int        Index,
    _In_ int        Count)
{
    // Calculate the block index first
    int BlockIndex  = Index / (sizeof(uintptr_t) * 8);
    int BlockOffset = Index % (sizeof(uintptr_t) * 8);
    int BitsLeft    = Count;
    int BitsSet     = 0;
    int NumberOfObjects;
    int i, j;
    assert(Bitmap != NULL);

    // Get maximum number of iterations
    NumberOfObjects = Bitmap->SizeInBytes / sizeof(uintptr_t);

    // Iterate the block and flip bits
    for (i = BlockIndex; (i < NumberOfObjects) && (BitsLeft > 0); i++) {
        for (j = BlockOffset; (j < __BITS) && (BitsLeft > 0); j++, BitsLeft--) {
            if (!(Bitmap->Data[i] & (1 << j))) {
                Bitmap->Data[i] |= (1 << j);
                BitsSet++;
            }
        }
        BlockOffset = 0;
    }
    return BitsSet;
}

/* BitmapClearBits
 * Clears all bits from the given index, and for <Count> bits. Returns the number
 * of bits cleared in this iteration. */
int
BitmapClearBits(
    _In_ Bitmap_t*  Bitmap,
    _In_ int        Index,
    _In_ int        Count)
{
    // Calculate the block index first
    int BlockIndex  = Index / __BITS;
    int BlockOffset = Index % __BITS;
    int BitsLeft    = Count;
    int BitsCleared = 0;
    int NumberOfObjects;
    int i, j;
    assert(Bitmap != NULL);

    // Get maximum number of iterations
    NumberOfObjects = Bitmap->SizeInBytes / sizeof(uintptr_t);

    // Iterate the block and flip bits
    for (i = BlockIndex; (i < NumberOfObjects) && (BitsLeft > 0); i++) {
        for (j = BlockOffset; (j < __BITS) && (BitsLeft > 0); j++, BitsLeft--) {
            if (Bitmap->Data[i] & (1 << j)) {
                Bitmap->Data[i] &= ~(1 << j);
                BitsCleared++;
            }
        }
        BlockOffset = 0;
    }
    return BitsCleared;
}

/* BitmapAreBitsSet
 * If all bits are set from the given index, and for <Count> bits, then this
 * function returns 1. Otherwise 0. */
int
BitmapAreBitsSet(
    _In_ Bitmap_t*  Bitmap,
    _In_ int        Index,
    _In_ int        Count)
{
    // Calculate the block index first
    int BlockIndex  = Index / (sizeof(uintptr_t) * 8);
    int BlockOffset = Index % (sizeof(uintptr_t) * 8);
    int BitsLeft    = Count;
    int i, j;
    assert(Bitmap != NULL);

    // Iterate the block and flip bits
    for (i = BlockIndex; 
         i < (Bitmap->SizeInBytes / sizeof(uintptr_t)) && BitsLeft > 0; i++) {
        for (j = BlockOffset; j < __BITS && BitsLeft > 0; j++, BitsLeft--) {
            if (!(Bitmap->Data[i] & (1 << j))) {
                return 0;
            }
        }

        // Reset block offset
        if (j == __BITS) {
            BlockOffset = 0;
        }
    }
    return 1;
}

/* BitmapAreBitsClear
 * If all bits are cleared from the given index, and for <Count> bits, then this
 * function returns 1. Otherwise 0. */
int
BitmapAreBitsClear(
    _In_ Bitmap_t*  Bitmap,
    _In_ int        Index,
    _In_ int        Count)
{
    // Calculate the block index first
    int BlockIndex  = Index / (sizeof(uintptr_t) * 8);
    int BlockOffset = Index % (sizeof(uintptr_t) * 8);
    int BitsLeft    = Count;
    int i, j;
    assert(Bitmap != NULL);

    // Iterate the block and flip bits
    for (i = BlockIndex; 
         i < (Bitmap->SizeInBytes / sizeof(uintptr_t)) && BitsLeft > 0; i++) {
        for (j = BlockOffset; j < __BITS && BitsLeft > 0; j++, BitsLeft--) {
            if (Bitmap->Data[i] & (1 << j)) {
                return 0;
            }
        }

        // Reset block offset
        if (j == __BITS) {
            BlockOffset = 0;
        }
    }
    return 1;
}

/* BitmapFindBits
 * Locates the requested number of consequtive free bits.
 * Returns the index of the first free bit. Returns -1 on no free. */
int
BitmapFindBits(
    _In_ Bitmap_t*  Bitmap,
    _In_ int        Count)
{
    // Variables
    int StartBit = -1;
    int i, j, k;
    assert(Bitmap != NULL);

    // Iterate bits
    for (i = 0; i < (Bitmap->SizeInBytes / sizeof(uintptr_t)); i++) {
        // Quick test, if all is allocated, damn
        if (Bitmap->Data[i] == __MASK) {
            continue;
        }

        // Test each bit in the value
        for (j = 0; j < __BITS; j++) {
            uintptr_t CurrentBit = 1 << j;
            if (Bitmap->Data[i] & CurrentBit) {
                continue;
            }

            // Ok, now we have to incremently make sure
            // enough consecutive bits are free
            for (k = 0; k < Count; k++) {
                // Make sure we are still in same block
                if ((j + k) >= __BITS) {
                    int TempI   = i + ((j + k) / __BITS);
                    int OffsetI = (j + k) % __BITS;
                    if (Bitmap->Data[TempI] & (1 << OffsetI)) {
                        break;
                    }
                }
                else {
                    if (Bitmap->Data[i] & (1 << (j + k))) {
                        break;
                    }
                }
            }

            // If k == numblocks we've found free bits!
            if (k == Count) {
                StartBit = (int)(i * (sizeof(uintptr_t) * 8) + j);
                break;
            }
        }

        // Break out if we found start-bit
        if (StartBit != -1) {
            break;
        }
    }
    return StartBit;
}

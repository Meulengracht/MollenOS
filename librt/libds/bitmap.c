/**
 * MollenOS
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
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * Generic Bitmap Implementation
 */

#include <ds/bitmap.h>
#include <ds/ds.h>
#include <string.h>
#include <stddef.h>
#include <assert.h>

#define _BM_BITS sizeof(void*) * 8
#define _BM_MASK ((sizeof(void*) == 8) ? 0xFFFFFFFFFFFFFFFF : 0xFFFFFFFF)

Bitmap_t*
BitmapCreate(
    _In_ size_t Size)
{
    Bitmap_t* Bitmap = NULL;
    size_t*   Data   = NULL;
    assert(Size > 0);

    // Allocate a new bitmap and associated buffer
    Bitmap  = (Bitmap_t*)dsalloc(sizeof(Bitmap_t));
    Data    = (uintptr_t*)dsalloc(Size);
    if (Bitmap == NULL || Data == NULL) {
        return NULL;
    }

    BitmapConstruct(Bitmap, Data, Size);
    Bitmap->Cleanup = 1;
    return Bitmap;
}

void
BitmapConstruct(
    _In_ Bitmap_t* Bitmap,
    _In_ size_t*   Data,
    _In_ size_t    Size)
{
    assert(Bitmap != NULL);
    assert(Data != NULL);
    assert(Size > 0);

    // Fill in data
    memset(Data, 0, Size);
    Bitmap->Cleanup     = 0;
    
    Bitmap->Data        = Data;
    Bitmap->SizeInBytes = Size;
    Bitmap->BitCount    = (Size * 8);
}

void
BitmapDestroy(
    _In_ Bitmap_t* Bitmap)
{
    assert(Bitmap != NULL);

    // Should we cleanup bitmap?
    if (Bitmap->Cleanup != 0) {
        dsfree((void*)Bitmap->Data);
        dsfree((void*)Bitmap);
    }
}

int
BitmapSetBits(
    _In_    Bitmap_t* Bitmap,
    _InOut_ int*      SearchIndex,
    _In_    int       Index,
    _In_    int       Count)
{
    // Calculate the block index first
    int BlockIndex  = Index / _BM_BITS;
    int BlockOffset = Index % _BM_BITS;
    int BitsLeft    = Count;
    int BitsSet     = 0;
    int NumberOfObjects;
    int i, j;
    assert(Bitmap != NULL);

    // Get maximum number of iterations
    NumberOfObjects = Bitmap->SizeInBytes / sizeof(size_t);
    
    // Update the search index if the bits we are setting overlaps
    // on the search index
    if (SearchIndex != NULL && *SearchIndex != -1) {
        if (Index <= *SearchIndex && (Index + Count) > *SearchIndex) {
            *SearchIndex = Index + Count;
        }
    }

    // Iterate the block and flip bits
    for (i = BlockIndex; (i < NumberOfObjects) && (BitsLeft > 0); i++) {
        if (Bitmap->Data[i] == 0 && BlockOffset == 0 && BitsLeft >= _BM_BITS) {
            Bitmap->Data[i] = _BM_MASK;
            BitsSet        += _BM_BITS;
            BitsLeft       -= _BM_BITS;
            continue;
        }
        
        for (j = BlockOffset; (j < _BM_BITS) && (BitsLeft > 0); j++, BitsLeft--) {
            size_t Bit = (size_t)1 << j;
            if (!(Bitmap->Data[i] & Bit)) {
                Bitmap->Data[i] |= Bit;
                BitsSet++;
            }
        }
        BlockOffset = 0;
    }
    return BitsSet;
}

int
BitmapClearBits(
    _In_    Bitmap_t* Bitmap,
    _InOut_ int*      SearchIndex,
    _In_    int       Index,
    _In_    int       Count)
{
    // Calculate the block index first
    int BlockIndex  = Index / _BM_BITS;
    int BlockOffset = Index % _BM_BITS;
    int BitsLeft    = Count;
    int BitsCleared = 0;
    int NumberOfObjects;
    int i, j;
    assert(Bitmap != NULL);

    // Get maximum number of iterations
    NumberOfObjects = Bitmap->SizeInBytes / sizeof(size_t);

    // Update the search index if the range we are clearing comes before the search index
    if (SearchIndex != NULL && (*SearchIndex == -1 || Index < *SearchIndex)) {
        *SearchIndex = Index;
    }
    
    // Iterate the block and flip bits
    for (i = BlockIndex; (i < NumberOfObjects) && (BitsLeft > 0); i++) {
        if (Bitmap->Data[i] == _BM_MASK && BlockOffset == 0 && BitsLeft >= _BM_BITS) {
            Bitmap->Data[i] = 0;
            BitsCleared    += _BM_BITS;
            BitsLeft       -= _BM_BITS;
            continue;
        }
        
        for (j = BlockOffset; (j < _BM_BITS) && (BitsLeft > 0); j++, BitsLeft--) {
            size_t Bit = (size_t)1 << j;
            if (Bitmap->Data[i] & Bit) {
                Bitmap->Data[i] &= ~Bit;
                BitsCleared++;
            }
        }
        BlockOffset = 0;
    }
    return BitsCleared;
}

int
BitmapAreBitsSet(
    _In_ Bitmap_t*  Bitmap,
    _In_ int        Index,
    _In_ int        Count)
{
    // Calculate the block index first
    int BlockIndex  = Index / _BM_BITS;
    int BlockOffset = Index % _BM_BITS;
    int BitsLeft    = Count;
    int NumberOfObjects;
    int i, j;
    assert(Bitmap != NULL);
    
    // Get maximum number of iterations
    NumberOfObjects = Bitmap->SizeInBytes / sizeof(size_t);

    // Iterate the block and flip bits
    for (i = BlockIndex; (i < NumberOfObjects) && (BitsLeft > 0); i++) {
        for (j = BlockOffset; j < _BM_BITS && BitsLeft > 0; j++, BitsLeft--) {
            size_t Bit = (size_t)1 << j;
            if (!(Bitmap->Data[i] & Bit)) {
                return 0;
            }
        }

        // Reset block offset
        if (j == _BM_BITS) {
            BlockOffset = 0;
        }
    }
    return 1;
}

int
BitmapAreBitsClear(
    _In_ Bitmap_t*  Bitmap,
    _In_ int        Index,
    _In_ int        Count)
{
    // Calculate the block index first
    int BlockIndex  = Index / _BM_BITS;
    int BlockOffset = Index % _BM_BITS;
    int BitsLeft    = Count;
    int NumberOfObjects;
    int i, j;
    assert(Bitmap != NULL);

    // Get maximum number of iterations
    NumberOfObjects = Bitmap->SizeInBytes / sizeof(size_t);

    // Iterate the block and flip bits
    for (i = BlockIndex; (i < NumberOfObjects) && (BitsLeft > 0); i++) {
        for (j = BlockOffset; j < _BM_BITS && BitsLeft > 0; j++, BitsLeft--) {
            size_t Bit = (size_t)1 << j;
            if (Bitmap->Data[i] & Bit) {
                return 0;
            }
        }

        // Reset block offset
        if (j == _BM_BITS) {
            BlockOffset = 0;
        }
    }
    return 1;
}

int
BitmapFindBits(
    _In_    Bitmap_t* Bitmap,
    _InOut_ int*      SearchIndex,
    _In_    int       Count)
{
    int NumberOfObjects;
    int StartBit = -1;
    int i = 0;
    int j = 0;
    int k;
    assert(Bitmap != NULL);
    
    // Get maximum number of iterations
    NumberOfObjects = Bitmap->SizeInBytes / sizeof(size_t);
    
    // Is the search index initialized?
    if (SearchIndex != NULL && *SearchIndex != -1) {
        i = *SearchIndex / _BM_BITS;
        j = *SearchIndex % _BM_BITS;
    }

    // Iterate bits
    for (; i < NumberOfObjects; i++) {
        // Quick test, if all is allocated, damn
        if (Bitmap->Data[i] == _BM_MASK) {
            continue;
        }

        // Test each bit in the value
        for (; j < _BM_BITS; j++) {
            size_t CurrentBit = (size_t)1 << j;
            if (Bitmap->Data[i] & CurrentBit) {
                continue;
            }

            // Ok, now we have to incremently make sure
            // enough consecutive bits are free
            for (k = 0; k < Count; k++) {
                // Make sure we are still in same block
                if ((j + k) >= _BM_BITS) {
                    int TempI   = i + ((j + k) / _BM_BITS);
                    int OffsetI = (j + k) % _BM_BITS;
                    if (Bitmap->Data[TempI] & ((size_t)1 << OffsetI)) {
                        break;
                    }
                }
                else {
                    if (Bitmap->Data[i] & ((size_t)1 << (j + k))) {
                        break;
                    }
                }
            }

            // If k == numblocks we've found free bits!
            if (k == Count) {
                StartBit = (int)(i * (sizeof(size_t) * 8) + j);
                break;
            }
        }
        j = 0; // Reset j

        // Break out if we found start-bit
        if (StartBit != -1) {
            break;
        }
    }
    
    // Update search index
    if (SearchIndex != NULL && *SearchIndex == StartBit) {
        *SearchIndex = StartBit + Count;
    }
    return StartBit;
}

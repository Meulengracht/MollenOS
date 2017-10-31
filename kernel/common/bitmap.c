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
 * MollenOS MCore - Generic Block Bitmap Implementation
 */

/* Includes 
 * - Library */
#include <blbitmap.h>
#include <stddef.h>
#include <string.h>

/* BlockBitmapCreate
 * Instantiate a new bitmap that keeps track of a
 * memory range between Start -> End with a given block size */
BlockBitmap_t*
BlockBitmapCreate(
    _In_ uintptr_t BlockStart, 
    _In_ uintptr_t BlockEnd, 
    _In_ size_t BlockSize)
{
    // Variables
    BlockBitmap_t *Blockmap = NULL;
    size_t Bytes = 0;

	// Allocate a new instance
	Blockmap = (BlockBitmap_t*)dsmalloc(sizeof(BlockBitmap_t));
    memset(Blockmap, 0, sizeof(BlockBitmap_t));

    // Store initial members
    Blockmap->BlockStart = BlockStart;
    Blockmap->BlockEnd = BlockEnd;
    Blockmap->BlockSize = BlockSize;
	Blockmap->BlockCount = (BlockEnd - BlockSize) / BlockSize;
	SpinlockReset(&Blockmap->Lock);

	// Now calculate blocks 
	// and divide by how many bytes are required
	Bytes = DIVUP((Blockmap->BlockCount + 1), 8);
    BitmapConstruct(&Blockmap->Base, (uintptr_t*)dsmalloc(Bytes), Bytes);
    Blockmap->Base.Cleanup = 1;
	return Blockmap;
}

/* BlockBitmapDestroy
 * Destroys a block bitmap, and releases 
 * all resources associated with the bitmap */
OsStatus_t
BlockBitmapDestroy(
    _In_ BlockBitmap_t *Blockmap)
{
	// Sanitize the input
	if (Blockmap == NULL) {
        return OsError;
    }
    return BitmapDestroy(&Blockmap->Base);
}

/* Allocates a number of bytes in the bitmap (rounded up in blocks)
 * and returns the calculated address of
 * the start of block allocated (continously) */
uintptr_t BitmapAllocateAddress(Bitmap_t *Bitmap, size_t Size)
{
	/* Variables */
	size_t NumBlocks = DIVUP(Size, Bitmap->BlockSize);
	size_t BlockItr, k;
	int BitItr;
	int StartBit = -1;

	/* Acquire lock */
	SpinlockAcquire(&Bitmap->Lock);

	/* Find time! */
	for (BlockItr = 0; BlockItr < Bitmap->BlockCount; BlockItr++)
	{
		/* Quick test, if all is allocated, damn */
		if (Bitmap->Bitmap[BlockItr] == __MASK) {
			continue;
		}

		/* Test each bit in this part */
		for (BitItr = 0; BitItr < __BITS; BitItr++) {
			size_t CurrentBit = 1 << BitItr;
			if (Bitmap->Bitmap[BlockItr] & CurrentBit) {
				continue;
			}

			/* Ok, now we have to incremently make sure
			 * enough consecutive bits are free */
			for (k = 0; k < NumBlocks; k++) {
				/* Sanitize that we haven't switched
				 * block temporarily */
				if ((BitItr + k) >= __BITS) {
					int TempI = BlockItr + ((BitItr + k) / __BITS);
					int OffsetI = (BitItr + k) % __BITS;
					size_t BlockBit = 1 << OffsetI;
					if (Bitmap->Bitmap[TempI] & BlockBit) {
						break;
					}
				}
				else {
					size_t BlockBit = 1 << (BitItr + k);
					if (Bitmap->Bitmap[BlockItr] & BlockBit) {
						break;
					}
				}
			}

			/* If k == numblocks we can allocate */
			if (k == NumBlocks) {
				StartBit = (int)(BlockItr * 4 * 8 + BitItr);
				break;
			}
		}

		/* Check for break */
		if (StartBit != -1)
			break;
	}

	/* Allocate the bits */
	if (StartBit != -1) {
		for (k = 0; k < NumBlocks; k++) {
			BitmapSet(Bitmap, StartBit + k);
		}

		/* Increase stats */
		Bitmap->BlocksAllocated += NumBlocks;
		Bitmap->NumAllocations++;
	}
	
	/* Release lock */
	SpinlockRelease(&Bitmap->Lock);

	/* Sanity 
	 * Only calculate the resulting 
	 * address if we found a bit */
	if (StartBit != -1) {
		return Bitmap->Base + (uintptr_t)(StartBit * Bitmap->BlockSize);
	}
	else
		return 0;
}

/* Deallocates a given address translated into offsets
 * into the given bitmap, and frees them in the bitmap */
void BitmapFreeAddress(Bitmap_t *Bitmap, uintptr_t Address, size_t Size)
{
	/* Start out by calculating the bit index */
	int StartBit = (Address - Bitmap->Base) / Bitmap->BlockSize;
	size_t i, NumBlocks = DIVUP(Size, Bitmap->BlockSize);

	/* Do some sanity checks on the calculated 
	 * values, they should be in bounds */
	if (StartBit < 0
		|| StartBit >= (int)Bitmap->BlockCount) {
		return;
	}

	/* Acquire lock */
	SpinlockAcquire(&Bitmap->Lock);

	/* Deallocate the bits */
	for (i = 0; i < NumBlocks; i++) {
		BitmapUnset(Bitmap, StartBit + i);
	}

	/* Release lock */
	SpinlockRelease(&Bitmap->Lock);

	/* Increase stats */
	Bitmap->BlocksAllocated -= NumBlocks;
	Bitmap->NumFrees++;
}

/* Validates the given address that it's within
 * range of our bitmap and that it has in fact, been allocated */
int BitmapValidateAddress(Bitmap_t *Bitmap, uintptr_t Address)
{
	/* Start out by calculating the bit index */
	int StartBit = (Address - Bitmap->Base) / Bitmap->BlockSize;

	/* Do some sanity checks on the calculated
	 * values, they should be in bounds */
	if (StartBit < 0
		|| StartBit >= (int)Bitmap->BlockCount) {
		return -1;
	}

	/* Now we will check whether or not the bit has
	 * been set, if it hasn't, it's not allocated */
	return BitmapTest(Bitmap, StartBit) == 1 ? 0 : 1;
}

/* MollenOS
*
* Copyright 2011 - 2016, Philip Meulengracht
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
* MollenOS MCore - Specialized (Memory) Bitmap
*/

/* Includes */
#include <Bitmap.h>
#include <Heap.h>
#include <Log.h>

/* C-Library */
#include <stddef.h>
#include <string.h>

/* Helper - Set
 * Sets a specific bit in the bitmap, which is
 * then marked as allocated */
void BitmapSet(Bitmap_t *Bitmap, int Bit) {
	Bitmap->Bitmap[Bit / __BITS] |= (1 << (Bit % __BITS));
}

/* Helper - Unset
 * Unsets a specific bit in the bitmap, which is
 * then marked as free for allocation */
void BitmapUnset(Bitmap_t *Bitmap, int Bit) {
	Bitmap->Bitmap[Bit / __BITS] &= ~(1 << (Bit % __BITS));
}

/* Helper - Test
 * Tests a specific bit in the bitmap, returns
 * 0 if the bit is free, 1 if otherwise */
int BitmapTest(Bitmap_t *Bitmap, int Bit)
{
	/* Get block & index */
	Addr_t Block = Bitmap->Bitmap[Bit / __BITS];
	Addr_t Index = (1 << (Bit % __BITS));

	/* Test */
	return ((Block & Index) != 0);
}

/* Instantiate a new bitmap that keeps track of a
 * memory range between Start -> End with a
 * given block size */
Bitmap_t *BitmapCreate(Addr_t Base, Addr_t End, size_t BlockSize)
{
	/* Allocate bitmap structure */
	Bitmap_t *Bitmap = (Bitmap_t*)kmalloc(sizeof(Bitmap_t));
	memset(Bitmap, 0, sizeof(Bitmap_t));

	/* Set initial stuff */
	Bitmap->Base = Base;
	Bitmap->End = End;
	Bitmap->Size = End - Base;
	Bitmap->BlockSize = BlockSize;

	/* Now calculate blocks 
	 * and divide by how many bytes are required */
	Bitmap->BlockCount = Bitmap->Size / BlockSize;
	Bitmap->BitmapSize = DIVUP((Bitmap->BlockCount + 1), 8);

	/* Now allocate bitmap */
	Bitmap->Bitmap = (Addr_t*)kmalloc(Bitmap->BitmapSize);

	/* Reset bitmap */
	memset(Bitmap->Bitmap, 0, Bitmap->BitmapSize);

	/* Reset lock */
	SpinlockReset(&Bitmap->Lock);

	/* Done! */
	return Bitmap;
}

/* Destroys a memory bitmap, and releases
 * all resources associated with the bitmap */
void BitmapDestroy(Bitmap_t *Bitmap)
{
	/* Sanity */
	if (Bitmap == NULL)
		return;

	/* Cleanup bitmap */
	kfree(Bitmap->Bitmap);
	kfree(Bitmap);
}

/* Allocates a number of bytes in the bitmap (rounded up in blocks)
 * and returns the calculated address of
 * the start of block allocated (continously) */
Addr_t BitmapAllocateAddress(Bitmap_t *Bitmap, size_t Size)
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
		return Bitmap->Base + (Addr_t)(StartBit * Bitmap->BlockSize);
	}
	else
		return 0;
}

/* Deallocates a given address translated into offsets
 * into the given bitmap, and frees them in the bitmap */
void BitmapFreeAddress(Bitmap_t *Bitmap, Addr_t Address, size_t Size)
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
int BitmapValidateAddress(Bitmap_t *Bitmap, Addr_t Address)
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

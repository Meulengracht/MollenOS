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

#include <string.h>

/* Helpers */
void BitmapSet(Bitmap_t *Bitmap, int Bit)
{
	Bitmap->Bitmap[Bit / 32] |= (1 << (Bit % 32));
}

void BitmapUnset(Bitmap_t *Bitmap, int Bit)
{
	Bitmap->Bitmap[Bit / 32] &= ~(1 << (Bit % 32));
}

int BitmapTest(Bitmap_t *Bitmap, int Bit)
{
	/* Get block & index */
	uint32_t Block = Bitmap->Bitmap[Bit / 32];
	uint32_t Index = (1 << (Bit % 32));

	/* Test */
	if (Block & Index)
		return 1;
	else
		return 0;
}

/* Instantiate a new bitmap that keeps track of a
 * memory range between Start -> End with a
 * given block size */
Bitmap_t *BitmapCreate(Addr_t Base, Addr_t End, size_t BlockSize)
{
	/* Allocate bitmap structure */
	Bitmap_t *Bitmap = (Bitmap_t*)kmalloc(sizeof(Bitmap_t));

	/* Set initial stuff */
	Bitmap->Base = Base;
	Bitmap->End = End;
	Bitmap->Size = End - Base;
	Bitmap->BlockSize = BlockSize;

	/* Now calculate blocks */
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
	size_t i, max = Bitmap->BlockCount;
	size_t numblocks = DIVUP(Size, Bitmap->BlockSize);
	int j, k;
	int rbit = -1;

	/* Acquire lock */
	SpinlockAcquire(&Bitmap->Lock);

	/* Find time! */
	for (i = 0; i < max; i++)
	{
		/* Quick test -> if all is allocated */
		if (Bitmap->Bitmap[i] != 0xFFFFFFFF)
		{
			/* Test each bit in this int */
			for (j = 0; j < 32; j++)
			{
				/* Calculate the bit */
				int bit = 1 << j;

				/* Test it */
				if (!(Bitmap->Bitmap[i] & bit))
				{
					/* Ok, so this is not completely enough 
					 * as we might have requested more than
					 * one block */
					if (numblocks != 1) {
						/* Test each bit in this int */
						for (k = 0; (k < (int)numblocks) && ((j + k) < 32); k++)
						{
							/* Calculate the bit */
							int kbit = 1 << (j + k);

							if (Bitmap->Bitmap[i] & kbit)
								break;
						}

						/* If k == numblocks we can allocate */
						if (k == (int)numblocks) {
							rbit = (int)(i * 4 * 8 + j);
							break;
						}
						else
							rbit = -1;
					}
					else {
						rbit = (int)(i * 4 * 8 + j);
						break;
					}
				}
			}
		}

		/* Check for break */
		if (rbit != -1)
			break;
	}

	/* Allocate the bits */
	for (k = 0; k < (int)numblocks; k++) {
		/* Allocate the bit */
		BitmapSet(Bitmap, rbit + k);
	}

	/* Release lock */
	SpinlockRelease(&Bitmap->Lock);

	/* Sanity */
	if (rbit != -1) {
		/* Calculate address */
		return Bitmap->Base + (Addr_t)(rbit * Bitmap->BlockSize);
	}
	else
		return 0;
}

/* Deallocates a given address translated into offsets
 * into the given bitmap, and frees them in the bitmap */
void BitmapFreeAddress(Bitmap_t *Bitmap, Addr_t Address, size_t Size)
{
	/* Start out by calculating the bit index */
	int rbit = (Address - Bitmap->Base) / Bitmap->BlockSize;
	size_t numblocks = DIVUP(Size, Bitmap->BlockSize);
	int i;

	/* Acquire lock */
	SpinlockAcquire(&Bitmap->Lock);

	/* Deallocate the bits */
	for (i = 0; i < (int)numblocks; i++) {
		/* Allocate the bit */
		BitmapUnset(Bitmap, rbit + i);
	}

	/* Release lock */
	SpinlockRelease(&Bitmap->Lock);
}
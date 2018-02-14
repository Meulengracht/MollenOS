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
#include <ds/blbitmap.h>
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
    size_t Bytes            = 0;

	// Allocate a new instance
	Blockmap = (BlockBitmap_t*)dsalloc(sizeof(BlockBitmap_t));
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
    BitmapConstruct(&Blockmap->Base, (uintptr_t*)dsalloc(Bytes), Bytes);
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

/* BlockBitmapAllocate
 * Allocates a number of bytes in the bitmap (rounded up in blocks)
 * and returns the calculated block of the start of block allocated (continously) */
uintptr_t
BlockBitmapAllocate(
    _In_ BlockBitmap_t *Blockmap,
    _In_ size_t Size)
{
    // Variables
    uintptr_t Block = 0;
	int BitCount    = 0;
    int Index       = -1;

    // Calculate number of bits
    BitCount = DIVUP(Size, Blockmap->BlockSize);

	// Locked operation
    SpinlockAcquire(&Blockmap->Lock);
    Index = BitmapFindBits(&Blockmap->Base, BitCount);
    if (Index != -1) {
        BitmapSetBits(&Blockmap->Base, Index, BitCount);
        Block = Blockmap->BlockStart + (uintptr_t)(Index * Blockmap->BlockSize);
        Blockmap->BlocksAllocated += BitCount;
		Blockmap->NumAllocations++;
    }
    SpinlockRelease(&Blockmap->Lock);
    return Block;
}

/* BlockBitmapFree
 * Deallocates a given block translated into offsets 
 * into the given bitmap, and frees them in the bitmap */
OsStatus_t
BlockBitmapFree(
    _In_ BlockBitmap_t* Blockmap,
    _In_ uintptr_t      Block,
    _In_ size_t         Size)
{
    // Variables
    OsStatus_t Result   = OsError;
    int BitCount        = 0;
	int Index           = -1;
    
    // Calculate the index and number of bits
    Index       = (Block - Blockmap->BlockStart) / Blockmap->BlockSize;
    BitCount    = DIVUP(Size, Blockmap->BlockSize);

	// Do some sanity checks on the calculated 
	// values, they should be in bounds
	if (Index < 0 || BitCount == 0
		|| Index >= (int)Blockmap->BlockCount) {
		return Result;
	}

	// Locked operation to free bits
    SpinlockAcquire(&Blockmap->Lock);
    Result = BitmapClearBits(&Blockmap->Base, Index, BitCount);
    if (Result == OsSuccess) {
        Blockmap->BlocksAllocated -= BitCount;
        Blockmap->NumFrees++;
    }
    SpinlockRelease(&Blockmap->Lock);
    return Result;
}

/* BlockBitmapValidate
 * Validates the given block that it's within
 * range of our bitmap and that it is either set or clear */
OsStatus_t
BlockBitmapValidateState(
    _In_ BlockBitmap_t *Blockmap,
    _In_ uintptr_t Block,
    _In_ int Set)
{
	// Variables
	int Index = -1;
    
    // Calculate the index and number of bits
    Index = (Block - Blockmap->BlockStart) / Blockmap->BlockSize;

	// Do some sanity checks on the calculated 
	// values, they should be in bounds
	if (Index < 0 || Index >= (int)Blockmap->BlockCount) {
		return OsError;
	}

	// Now we will check whether or not the bit has been set/clear
    if (Set) {
        return BitmapAreBitsSet(&Blockmap->Base, Index, 1) == 1 ? OsSuccess : OsError;
    }
    else {
        return BitmapAreBitsClear(&Blockmap->Base, Index, 1) == 1 ? OsSuccess : OsError;
    }
}

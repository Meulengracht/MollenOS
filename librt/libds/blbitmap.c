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

#include <ds/blbitmap.h>
#include <string.h>
#include <assert.h>

/* GetBytesNeccessaryForBlockmap
 * Calculates the number of bytes neccessary for the allocation parameters. */
size_t
GetBytesNeccessaryForBlockmap(
    _In_ uintptr_t          BlockStart, 
    _In_ uintptr_t          BlockEnd, 
    _In_ size_t             BlockSize)
{
    size_t BlockCount = (BlockEnd - BlockStart) / BlockSize;
    return DIVUP((BlockCount + 1), 8); // We can have 8 blocks per byte
}

/* CreateBlockmap
 * Creates a new blockmap of with the given configuration and returns a pointer to a newly
 * allocated blockmap. Also returns an error code. */
OsStatus_t
CreateBlockmap(
    _In_  Flags_t           Configuration,
    _In_  uintptr_t         BlockStart, 
    _In_  uintptr_t         BlockEnd, 
    _In_  size_t            BlockSize,
    _Out_ BlockBitmap_t**   Blockmap)
{
    // Variables
    BlockBitmap_t *Pointer  = NULL;
    void *Buffer            = NULL;

    Pointer = (BlockBitmap_t*)dsalloc(sizeof(BlockBitmap_t));
    Buffer  = dsalloc(GetBytesNeccessaryForBlockmap(BlockStart, BlockEnd, BlockSize));
    ConstructBlockmap(Pointer, Buffer, Configuration, BlockStart, BlockEnd, BlockSize);

    // Update user-provided pointer
    Pointer->Base.Cleanup = 1;
    *Blockmap = Pointer;
    return OsSuccess;;
}

/* ConstructBlockmap
 * Instantiates a static instance of a block bitmap. The buffer used for the bit storage
 * must also be provided and should be of at-least GetBytesNeccessaryForBlockmap(<Params>). */
OsStatus_t
ConstructBlockmap(
    _In_ BlockBitmap_t*     Blockmap,
    _In_ void*              Buffer,
    _In_ Flags_t            Configuration,
    _In_ uintptr_t          BlockStart, 
    _In_ uintptr_t          BlockEnd, 
    _In_ size_t             BlockSize)
{
    assert(Blockmap != NULL);
    assert(Buffer != NULL);

    // Store initial members
    memset(Blockmap, 0, sizeof(BlockBitmap_t));
    Blockmap->BlockStart    = BlockStart;
    Blockmap->BlockEnd      = BlockEnd;
    Blockmap->BlockSize     = BlockSize;
    Blockmap->BlockCount    = (BlockEnd - BlockStart) / BlockSize;
    BitmapConstruct(&Blockmap->Base, (size_t*)Buffer, DIVUP((Blockmap->BlockCount + 1), 8));

    // Handle configuration parameters
    if (Configuration & BLOCKMAP_ALLRESERVED) {
        memset(Buffer, 0xFF, DIVUP(Blockmap->BlockCount, 8));
        Blockmap->BlocksAllocated = Blockmap->BlockCount;
    }
    return OsSuccess;
}

/* DestroyBlockmap
 * Destroys a block bitmap, and releases all resources associated with the bitmap */
OsStatus_t
DestroyBlockmap(
    _In_ BlockBitmap_t* Blockmap)
{
    assert(Blockmap != NULL);
    return BitmapDestroy(&Blockmap->Base);
}

/* AllocateBlocksInBlockmap
 * Allocates a number of bytes in the bitmap (rounded up in blocks)
 * and returns the calculated block of the start of block allocated (continously) */
uintptr_t
AllocateBlocksInBlockmap(
    _In_ BlockBitmap_t* Blockmap,
    _In_ size_t         AllocationMask,
    _In_ size_t         Size)
{
    // Variables
    uintptr_t Block = 0;
    size_t BitCount;
    int Index;
    
    assert(Blockmap != NULL);
    assert(Size > 0);

    // Calculate number of bits
    BitCount = DIVUP(Size, Blockmap->BlockSize);

    // Locked operation
    dslock(&Blockmap->SyncObject);
    Index = BitmapFindBits(&Blockmap->Base, NULL, BitCount);
    if (Index != -1) {
        BitmapSetBits(&Blockmap->Base, NULL, Index, BitCount);
        Block = Blockmap->BlockStart + (uintptr_t)(Index * Blockmap->BlockSize);
        Blockmap->BlocksAllocated += BitCount;
        Blockmap->NumAllocations++;
    }
    dsunlock(&Blockmap->SyncObject);
    return Block;
}

/* ReserveBlockmapRegion
 * Reserves a region of the blockmap. This sets the given region to allocated. The
 * region and size must be within boundaries of the blockmap. */
OsStatus_t
ReserveBlockmapRegion(
    _In_ BlockBitmap_t* Blockmap,
    _In_ uintptr_t      Block,
    _In_ size_t         Size)
{
    int BitCount;
    int Index;

    // Sanitize the bounds
    assert(Blockmap != NULL);
    assert(Size > 0);
    if ((Block + Size) > Blockmap->BlockEnd) {
        return OsError;
    }

    // Calculate number of bits that we need to set, also calculate
    // the start bit
    Index    = (Block - Blockmap->BlockStart) / Blockmap->BlockSize;
    BitCount = DIVUP(Size, Blockmap->BlockSize);

    // Locked operation
    dslock(&Blockmap->SyncObject);
    BitCount = BitmapSetBits(&Blockmap->Base, NULL, Index, BitCount);
    if (BitCount != 0) {
        Blockmap->BlocksAllocated += BitCount;
        Blockmap->NumAllocations++;
    }
    dsunlock(&Blockmap->SyncObject);
    return OsSuccess;
}

/* ReleaseBlockmapRegion
 * Deallocates a given block translated into offsets 
 * into the given bitmap, and frees them in the bitmap */
OsStatus_t
ReleaseBlockmapRegion(
    _In_ BlockBitmap_t* Blockmap,
    _In_ uintptr_t      Block,
    _In_ size_t         Size)
{
    int BitCount;
    int Index;

    assert(Blockmap != NULL);
    assert(Size > 0);
    
    // Calculate the index and number of bits
    Index    = (Block - Blockmap->BlockStart) / Blockmap->BlockSize;
    BitCount = DIVUP(Size, Blockmap->BlockSize);

    // Do some sanity checks on the calculated 
    // values, they should be in bounds
    if (Index < 0 || BitCount == 0 || Index >= (int)Blockmap->BlockCount) {
        return OsError;
    }

    // Locked operation to free bits
    dslock(&Blockmap->SyncObject);
    BitCount = BitmapClearBits(&Blockmap->Base, NULL, Index, BitCount);
    if (BitCount != 0) {
        Blockmap->BlocksAllocated -= BitCount;
        Blockmap->NumFrees++;
    }
    dsunlock(&Blockmap->SyncObject);
    return OsSuccess;
}

/* BlockBitmapValidate
 * Validates the given block that it's within
 * range of our bitmap and that it is either set or clear */
OsStatus_t
BlockBitmapValidateState(
    _In_ BlockBitmap_t* Blockmap,
    _In_ uintptr_t      Block,
    _In_ int            Set)
{
    int Index;
    assert(Blockmap != NULL);
    
    // Calculate the index and number of bits
    Index = (Block - Blockmap->BlockStart) / Blockmap->BlockSize;

    // Do some sanity checks on the calculated 
    // values, they should be in bounds
    if (Index < 0 || Index >= (int)Blockmap->BlockCount) {
        return OsDoesNotExist;
    }

    // Now we will check whether or not the bit has been set/clear
    if (Set) {
        return BitmapAreBitsSet(&Blockmap->Base, Index, 1) == 1 ? OsSuccess : OsError;
    }
    else {
        return BitmapAreBitsClear(&Blockmap->Base, Index, 1) == 1 ? OsSuccess : OsError;
    }
}

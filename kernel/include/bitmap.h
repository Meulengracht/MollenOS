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

#ifndef _MOLLENOS_BITMAP_H_
#define _MOLLENOS_BITMAP_H_

/* Includes 
 * - C-Library */
#include <os/osdefs.h>

/* Includes
 * - System */
#include <Arch.h>
#include <os/Spinlock.h>

/* Structures */
typedef struct _Bitmap
{
	/* Base/End address */
	Addr_t Base;
	Addr_t End;

	/* Memory Size */
	size_t Size;

	/* Block Information */
	size_t BlockSize;
	size_t BlockCount;

	/* Bitmap statistics
	 * Blocks in use, etc */
	size_t BlocksAllocated;
	size_t NumAllocations;
	size_t NumFrees;

	/* Data */
	Addr_t *Bitmap;
	size_t BitmapSize;

	/* Lock */
	Spinlock_t Lock;

} Bitmap_t;

/* Instantiate a new bitmap that keeps track of a
 * memory range between Start -> End with a 
 * given block size */
__EXTERN Bitmap_t *BitmapCreate(Addr_t Base, Addr_t End, size_t BlockSize);

/* Destroys a memory bitmap, and releases 
 * all resources associated with the bitmap */
__EXTERN void BitmapDestroy(Bitmap_t *Bitmap);

/* Allocates a number of bytes in the bitmap (rounded up in blocks)
 * and returns the calculated address of 
 * the start of block allocated (continously) */
__EXTERN Addr_t BitmapAllocateAddress(Bitmap_t *Bitmap, size_t Size);

/* Deallocates a given address translated into offsets 
 * into the given bitmap, and frees them in the bitmap */
__EXTERN void BitmapFreeAddress(Bitmap_t *Bitmap, Addr_t Address, size_t Size);

/* Validates the given address that it's within
 * range of our bitmap and that it has in fact, been allocated */
__EXTERN int BitmapValidateAddress(Bitmap_t *Bitmap, Addr_t Address);

#endif //!_MOLLENOS_BITMAP_H_
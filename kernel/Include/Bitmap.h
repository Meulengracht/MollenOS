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

/* Includes */
#include <crtdefs.h>
#include <stdint.h>

/* OS */
#include <Arch.h>

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

	/* Data */
	Addr_t *Bitmap;
	size_t BitmapSize;

	/* Lock */
	Spinlock_t Lock;

} Bitmap_t;

/* Instantiate a new bitmap that keeps track of a
 * memory range between Start -> End with a 
 * given block size */
_CRT_EXTERN Bitmap_t *BitmapCreate(Addr_t Base, Addr_t End, size_t BlockSize);

/* Destroys a memory bitmap, and releases 
 * all resources associated with the bitmap */
_CRT_EXTERN void BitmapDestroy(Bitmap_t *Bitmap);

/* Allocates a number of bytes in the bitmap (rounded up in blocks)
 * and returns the calculated address of 
 * the start of block allocated (continously) */
_CRT_EXTERN Addr_t BitmapAllocateAddress(Bitmap_t *Bitmap, size_t Size);

/* Deallocates a given address translated into offsets 
 * into the given bitmap, and frees them in the bitmap */
_CRT_EXTERN void BitmapFreeAddress(Bitmap_t *Bitmap, Addr_t Address, size_t Size);

#endif //!_MOLLENOS_BITMAP_H_
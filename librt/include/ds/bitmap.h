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

#ifndef _GENERIC_BITMAP_H_
#define _GENERIC_BITMAP_H_

/* Includes 
 * - Library */
#include <os/osdefs.h>
#include <ds/ds.h>

/* Bitmap
 * Contains information about the bitmap and resources. */
typedef struct _Bitmap {
    int                  Cleanup;
    size_t               SizeInBytes;
    size_t               Capacity;
	uintptr_t           *Data;
} Bitmap_t;

/* BitmapCreate
 * Creates a bitmap of the given size in bytes, the actual available
 * member count will then be Size * sizeof(byte). This automatically
 * allocates neccessary resources */
MOSAPI
Bitmap_t*
MOSABI
BitmapCreate(
    _In_ size_t Size);

/* BitmapConstruct
 * Creates a bitmap of the given size in bytes, the actual available
 * member count will then be Size * sizeof(byte). This uses user-provided
 * resources, and won't be cleaned up. */
MOSAPI
OsStatus_t
MOSABI
BitmapConstruct(
    _In_ Bitmap_t *Bitmap,
    _In_ uintptr_t *Data,
    _In_ size_t Size);

/* BitmapDestroy
 * Cleans up any resources allocated by the Create/Construct. */
MOSAPI
OsStatus_t
MOSABI
BitmapDestroy(
    _In_ Bitmap_t *Bitmap);

/* BitmapSetBits
 * Flips all bits to 1 at the given index, and for <Count> bits. */
MOSAPI
OsStatus_t
MOSABI
BitmapSetBits(
    _In_ Bitmap_t *Bitmap,
    _In_ int Index,
    _In_ int Count);

/* BitmapClearBits
 * Clears all bits from the given index, and for <Count> bits. */
MOSAPI
OsStatus_t
MOSABI
BitmapClearBits(
    _In_ Bitmap_t *Bitmap,
    _In_ int Index,
    _In_ int Count);

/* BitmapAreBitsSet
 * If all bits are set from the given index, and for <Count> bits, then this
 * function returns 1. Otherwise 0. */
MOSAPI
int
MOSABI
BitmapAreBitsSet(
    _In_ Bitmap_t *Bitmap,
    _In_ int Index,
    _In_ int Count);

/* BitmapAreBitsClear
 * If all bits are cleared from the given index, and for <Count> bits, then this
 * function returns 1. Otherwise 0. */
MOSAPI
int
MOSABI
BitmapAreBitsClear(
    _In_ Bitmap_t *Bitmap,
    _In_ int Index,
    _In_ int Count);

/* BitmapFindBits
 * Locates the requested number of consequtive free bits.
 * Returns the index of the first free bit. Returns -1 on no free. */
MOSAPI
int
MOSABI
BitmapFindBits(
    _In_ Bitmap_t *Bitmap,
    _In_ int Count);

#endif //!_GENERIC_BITMAP_H_

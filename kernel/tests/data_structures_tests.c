/* MollenOS
 *
 * Copyright 2011 - 2018, Philip Meulengracht
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
 * OS Testing Suite
 *  - Synchronization tests to verify the robustness and correctness of concurrency.
 */
#define __MODULE "TEST"
#define __TRACE

#include <ds/blbitmap.h>
#include <debug.h>

/* TestDataStructures
 * Performs tests with the data structures used in the OS to verify stability and
 * robustness of their implementations. */
void
TestDataStructures(void *Unused)
{
    // Variables
    BlockBitmap_t *Bitmap;
    uintptr_t Allocation, Allocation1;
    _CRT_UNUSED(Unused);

    // Debug
    TRACE("TestDataStructures()");


    // Create the bitmap
    Bitmap = BlockBitmapCreate(0, 10000, 1);
    TRACE(" > Allocating 1000 blocks, then allocating 300 more");
    Allocation = BlockBitmapAllocate(Bitmap, 1000);
    Allocation1 = BlockBitmapAllocate(Bitmap, 300);
    TRACE(" > Allocation (1000) => %u", Allocation);
    TRACE(" > Allocation (300) => %u", Allocation1);
    TRACE(" > Freeing the 300, reallocing 120, then freeing 1000 and allocating 3000");
    BlockBitmapFree(Bitmap, Allocation1, 300);
    Allocation1 = BlockBitmapAllocate(Bitmap, 120);
    BlockBitmapFree(Bitmap, Allocation, 1000);
    Allocation = BlockBitmapAllocate(Bitmap, 3000);
    TRACE(" > Allocation (120) => %u", Allocation1);
    TRACE(" > Allocation (3000) => %u", Allocation);
    BlockBitmapFree(Bitmap, Allocation, 3000);
    BlockBitmapFree(Bitmap, Allocation1, 120);
    BlockBitmapDestroy(Bitmap);
}

/* MollenOS
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
 * along with this program.If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * MollenOS x86-32 Memory Definitions, Structures, Explanations
 */

#ifndef __X86_32_PAGING__
#define __X86_32_PAGING__

#include <os/osdefs.h>

/* Paging Definitions
 * Defines paging structure sizes for the different hardware paging structures. */
#define ENTRIES_PER_PAGE        1024
#define PAGE_SIZE               0x1000
#define PAGE_MASK               0xFFFFF000
#define ATTRIBUTE_MASK          0x00000FFF

/* Table sizes, in 32 bit a table is 4mb 
 * and this means we need to identity a single page-table. */
#define TABLE_SPACE_SIZE        0x400000
#define MEMORY_ALLOCATION_MASK  0x3FFFFF

/* Indices
 * 10 bits each are used for each part, with the first 12 bits reserved */
#define PAGE_DIRECTORY_INDEX(x) (((x) >> 22) & 0x3FF)
#define PAGE_TABLE_INDEX(x)     (((x) >> 12) & 0x3FF)

/* Page Table Structure
 * Denotes how the paging structure is for the X86-32
 * platform, this is different from X86-64 */
PACKED_TYPESTRUCT(PageTable, {
    _Atomic(uint32_t)   Pages[ENTRIES_PER_PAGE];
});

/* Page Directory Structure
 * Denotes how the paging structure is for the X86-32
 * platform, this is different from X86-64 */
PACKED_TYPESTRUCT(PageDirectory, {
    _Atomic(uint32_t)   pTables[ENTRIES_PER_PAGE];    // Seen by MMU
    uint32_t            vTables[ENTRIES_PER_PAGE];    // Not seen by MMU
});

#endif //!__X86_32_PAGING__

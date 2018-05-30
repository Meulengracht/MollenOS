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
 * MollenOS x86-64 Memory Definitions, Structures, Explanations
 */

#ifndef _X86_64_PAGING_H_
#define _X86_64_PAGING_H_

/* Includes 
 * - System */
#include <os/osdefs.h>

/* Paging Definitions
 * Defines paging structure sizes for the different hardware paging structures. */
#define ENTRIES_PER_PAGE        512
#define PAGE_SIZE               0x1000
#define PAGE_MASK               0xFFFFFFFFFFFFF000
#define ATTRIBUTE_MASK          0x0000000000000FFF

/* Table sizes, in 64 bit a table is 2mb 
 * and this means we need to identity map more than a single page-table. */
#define TABLE_SPACE_SIZE        0x200000
#define DIRECTORY_SPACE_SIZE    0x40000000
#define MEMORY_ALLOCATION_MASK  0x3FFFFF

/* Indices
 * 9 bits each are used for each part, with the first 12 bits reserved */
#define PAGE_LEVEL_4_INDEX(x)           (((x) >> 39) & 0x1FF)
#define PAGE_DIRECTORY_POINTER_INDEX(x) (((x) >> 30) & 0x1FF)
#define PAGE_DIRECTORY_INDEX(x)         (((x) >> 21) & 0x1FF)
#define PAGE_TABLE_INDEX(x)             (((x) >> 12) & 0x1FF)

/* Page Table Structure
 * Contains a table of pages (level 1) */
PACKED_TYPESTRUCT(PageTable, {
    uint64_t        Pages[ENTRIES_PER_PAGE];
});

/* Page Directory Structure
 * Contains a table of page tables (level 2) */
PACKED_TYPESTRUCT(PageDirectory, {
    uint64_t        pTables[ENTRIES_PER_PAGE];    // Seen by MMU
    uint64_t        vTables[ENTRIES_PER_PAGE];    // Not seen by MMU
});

/* Page Directory Table Structure
 * Contains a table of page directories (level 3) */
PACKED_TYPESTRUCT(PageDirectoryTable, {
    uint64_t        pTables[ENTRIES_PER_PAGE];    // Seen by MMU
    uint64_t        vTables[ENTRIES_PER_PAGE];    // Not seen by MMU
});

/* Page Master Table Structure
 * Contains a table of page directory tables (level 4) */
PACKED_TYPESTRUCT(PageMasterTable, {
    uint64_t        pTables[ENTRIES_PER_PAGE];    // Seen by MMU
    uint64_t        vTables[ENTRIES_PER_PAGE];    // Not seen by MMU
});


#endif //!_X86_64_PAGING_H_

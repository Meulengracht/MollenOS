/**
 * MollenOS
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
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * MollenOS x86-64 Memory Definitions, Structures, Explanations
 */

#ifndef __X86_64_PAGING__
#define __X86_64_PAGING__

#include <os/osdefs.h>

/**
 * Paging Definitions
 * Defines paging structure sizes for the different hardware paging structures.
 */
#define ENTRIES_PER_PAGE        512
#define PAGE_SIZE               0x1000U
#define PAGE_MASK               0xFFFFFFFFFFFFF000ULL
#define ATTRIBUTE_MASK          0x0000000000000FFFULL

/**
 * Table sizes, in 64 bit a table is 2mb
 * and this means we need to identity map more than a single page-table.
 * PageTable - 2Mb
 * PageDirectory - 1GB
 * PageDirectoryTable - 512GB
 * PML4 - 262.1TB
 */
#define TABLE_SPACE_SIZE           (PAGE_SIZE * ENTRIES_PER_PAGE)
#define DIRECTORY_SPACE_SIZE       ((uint64_t)TABLE_SPACE_SIZE * (uint64_t)ENTRIES_PER_PAGE)
#define DIRECTORY_TABLE_SPACE_SIZE ((uint64_t)DIRECTORY_SPACE_SIZE * (uint64_t)ENTRIES_PER_PAGE)
#define PML4_SPACE_SIZE            ((uint64_t)DIRECTORY_TABLE_SPACE_SIZE * (uint64_t)ENTRIES_PER_PAGE)
#define MEMORY_ALLOCATION_MASK     0x1FFFFFU

/**
 * Page directory indices
 * 9 bits each are used for each part, with the first 12 bits reserved
 */
#define PAGE_LEVEL_4_INDEX(x)           (((x) >> 39U) & 0x1FFU)
#define PAGE_DIRECTORY_POINTER_INDEX(x) (((x) >> 30U) & 0x1FFU)
#define PAGE_DIRECTORY_INDEX(x)         (((x) >> 21U) & 0x1FFU)
#define PAGE_TABLE_INDEX(x)             (((x) >> 12U) & 0x1FFU)

/**
 * Page table structure
 * The lowest table structure, contains page-entries (level 1)
 */
PACKED_TYPESTRUCT(PageTable, {
    _Atomic(uint64_t) Pages[ENTRIES_PER_PAGE];
});

/**
 * Page directory structure
 * Contains a table of page tables (level 2)
 */
PACKED_TYPESTRUCT(PageDirectory, {
    _Atomic(uint64_t) pTables[ENTRIES_PER_PAGE];    // Seen by MMU
    uint64_t          vTables[ENTRIES_PER_PAGE];    // Not seen by MMU
});

/**
 * Page directory Table Structure
 * Contains a table of page directories (level 3)
 */
PACKED_TYPESTRUCT(PageDirectoryTable, {
    _Atomic(uint64_t) pTables[ENTRIES_PER_PAGE];    // Seen by MMU
    uint64_t          vTables[ENTRIES_PER_PAGE];    // Not seen by MMU
});

/**
 * Page Master Table Structure
 * Contains a table of page directory tables (level 4)
 */
PACKED_TYPESTRUCT(PageMasterTable, {
    _Atomic(uint64_t) pTables[ENTRIES_PER_PAGE];    // Seen by MMU
    uint64_t          vTables[ENTRIES_PER_PAGE];    // Not seen by MMU
});

#define PAGE_MASTER_LEVEL PageMasterTable_t

#endif //!__X86_64_PAGING__

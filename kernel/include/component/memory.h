/* MollenOS
 *
 * Copyright 2018, Philip Meulengracht
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
 * MollenOS System Component Infrastructure 
 * - The Memory component. This component has the task of managing
 *   different memory regions that map to physical components
 */

#ifndef __COMPONENT_MEMORY__
#define __COMPONENT_MEMORY__

#include <os/osdefs.h>

typedef struct SystemMemoryRange {
    uintptr_t Start;
    size_t    Length;
} SystemMemoryRange_t;

typedef struct SystemMemoryMap {
    SystemMemoryRange_t KernelRegion;
    SystemMemoryRange_t UserCode;
    SystemMemoryRange_t UserHeap;
    SystemMemoryRange_t ThreadRegion;
} SystemMemoryMap_t;

typedef struct SystemMemory {
    // Memory Information
    uintptr_t            Start;
    uintptr_t            Length;
    size_t               BlockSize;
    int                  Removable;      // This memory is not fixed and should not be used for system memory
    int                  NonVolatile;    // Memory is non volatile
} SystemMemory_t;

#define IS_KERNEL_CODE(mmap_ptr, addr) (ISINRANGE(addr, (mmap_ptr)->KernelRegion.Start, (mmap_ptr)->KernelRegion.Start + (mmap_ptr)->KernelRegion.Length))
#define IS_USER_CODE(mmap_ptr, addr)   (ISINRANGE(addr, (mmap_ptr)->UserCode.Start, (mmap_ptr)->UserCode.Start + (mmap_ptr)->UserCode.Length))
#define IS_USER_STACK(mmap_ptr, addr)  (ISINRANGE(addr, (mmap_ptr)->ThreadRegion.Start, (mmap_ptr)->ThreadRegion.Start + (mmap_ptr)->ThreadRegion.Length))

#endif // !__COMPONENT_MEMORY__

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
 * along with this program.If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * Contract Definitions & Structures (Storage Contract)
 * - This header describes the base contract-structure, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#ifndef __DDK_STORAGE_H__
#define __DDK_STORAGE_H__

#include <ddk/ddkdefs.h>

#define __STORAGE_OPERATION_READ            0x00000001
#define __STORAGE_OPERATION_WRITE           0x00000002

typedef struct StorageDescriptor {
    UUId_t   Device;
    UUId_t   Driver;
    unsigned int  Flags;
    char     Model[64];
    char     Serial[32];
    size_t   SectorSize;
    uint64_t SectorCount;
    size_t   SectorsPerCylinder;
    size_t   LUNCount;
} StorageDescriptor_t;

#endif //!__DDK_STORAGE_H__

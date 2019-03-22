/* MollenOS
 *
 * Copyright 2019, Philip Meulengracht
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
 * Storage Type Definitions & Structures
 * - This header describes the base storage-structure, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#ifndef __TYPES_STORAGE_H__
#define __TYPES_STORAGE_H__

#include <os/osdefs.h>

typedef struct {
    long           Id;
    Flags_t        Flags;
    char           SerialNumber[32];
    unsigned long  SectorSize;
    LargeInteger_t SectorsTotal;
} OsStorageDescriptor_t;

// OsStorageDescriptor_t::Flags
#define STORAGE_STATIC          0x00000001 // Storage is not hot
#define STORAGE_READONLY        0x00000002 // Storage cannot be modified

#endif //!__TYPES_STORAGE_H__

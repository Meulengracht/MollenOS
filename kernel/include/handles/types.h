/**
 * MollenOS
 *
 * Copyright, Philip Meulengracht
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
 */

#ifndef __HANDLES_TYPES_H__
#define __HANDLES_TYPES_H__

#include <os/types/grants.h>

typedef enum HandleType {
    HandleTypeGeneric = 0,
    HandleTypeSet,
    HandleTypeMemorySpace,
    HandleTypeMSContext,
    HandleTypeThread,
    HandleTypeUserEvent,
    HandleTypeSHM,
    HandleTypeSHMAccess,
    HandleTypeGrantToken,
    HandleTypeSHMView
} HandleType_t;

typedef void (*HandleDestructorFn)(void*);

#endif //! __HANDLES_TYPES_H__

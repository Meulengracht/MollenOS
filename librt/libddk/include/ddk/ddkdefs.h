/**
 * MollenOS
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
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * DDK Definitions & Structures
 * - This header describes the base ddk-structure, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#ifndef __DDK_DEFINITIONS_H__
#define __DDK_DEFINITIONS_H__

#include <os/mollenos.h>

#define DDKDECL(ReturnType, Function) extern ReturnType Function
#define DDKDECL_DATA(Type, Name) extern Type Name

#endif //!__DDK_DEFINITIONS_H__

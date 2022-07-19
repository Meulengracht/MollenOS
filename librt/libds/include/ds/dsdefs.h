/* MollenOS
 *
 * Copyright 2011, Philip Meulengracht
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
 * Generic Data Structures
 */

#ifndef __DS_DSDEFS_H__
#define __DS_DSDEFS_H__

#if !defined(VALI)
#include <stddef.h>
#include <stdint.h>

typedef uint32_t uuid_t;
#define _CODE_BEGIN
#define _CODE_END
#define _In_
#define _Out_
#define _InOut_
#else
#include <os/osdefs.h>
#endif

#define DSDECL(ReturnType, Function) extern ReturnType Function
#define DSDECL_DATA(Type, Name) extern Type Name

#endif //!__DS_DSDEFS_H__

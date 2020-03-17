/**
 * MollenOS
 *
 * Copyright 2020, Philip Meulengracht
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
 * Guard header library
 * - Implementation of guard semantics for checking parameters and return values.
 */
 
#ifndef __GUARD_H__
#define __GUARD_H__

#include <assert.h>

#define GUARD_AGAINST_NULL(Param) assert((Param != NULL) && __FILE__ ", " __LINE__ ": " ##Param " is null")

#endif //!__GUARD_H__

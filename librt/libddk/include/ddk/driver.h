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
 * Driver Base Inteface
 * - Describes the base driver interface, which calls should be implemented and
 *   which calls are available for communicating with drivers
 */

#ifndef __DDK_DRIVER_H__
#define __DDK_DRIVER_H__

#define __DRIVER_REGISTERINSTANCE   (int)0
#define __DRIVER_UNREGISTERINSTANCE	(int)1
#define __DRIVER_QUERYCONTRACT      (int)2
#define __DRIVER_UNLOAD             (int)3

#endif //!__DDK_DRIVER_H__

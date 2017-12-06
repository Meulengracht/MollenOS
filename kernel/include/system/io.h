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
 * MollenOS Io Interface
 * - Contains a glue layer to access hardware-io functionality
 *   that all sub-layers / architectures must conform to
 */
#ifndef _MCORE_IO_H_
#define _MCORE_IO_H_

/* Includes
 * - Library */
#include <os/osdefs.h>

/* Io Systems Definitions 
 * Contains bit definitions and magic constants */
#define IO_SOURCE_MEMORY            0
#define IO_SOURCE_HARDWARE          1

/* IoRead 
 * Reads a value from the given data source. Accepted values in
 * width are 1, 2, 4 or 8. */
KERNELAPI
OsStatus_t
KERNELABI
IoRead(
    _In_ int        Source,
    _In_ uintptr_t  Address,
    _In_ size_t     Width,
    _Out_ size_t   *Value);

/* IoWrite 
 * Writes a value to the given data source. Accepted values in
 * width are 1, 2, 4 or 8. */
KERNELAPI
OsStatus_t
KERNELABI
IoWrite(
    _In_ int        Source,
    _In_ uintptr_t  Address,
    _In_ size_t     Width,
    _In_ size_t     Value);

/* PciRead
 * Reads a value from the given pci address. Accepted values in
 * width are 1, 2, 4 or 8. */
KERNELAPI
OsStatus_t
KERNELABI
PciRead(
    _In_ unsigned   Bus,
    _In_ unsigned   Slot,
    _In_ unsigned   Function,
    _In_ unsigned   Register,
    _In_ size_t     Width,
    _Out_ size_t   *Value);

/* PciWrite
 * Writes a value to the given pci address. Accepted values in
 * width are 1, 2, 4 or 8. */
KERNELAPI
OsStatus_t
KERNELABI
PciWrite(
    _In_ unsigned   Bus,
    _In_ unsigned   Slot,
    _In_ unsigned   Function,
    _In_ unsigned   Register,
    _In_ size_t     Width,
    _In_ size_t     Value);

#endif //!_MCORE_IO_H_

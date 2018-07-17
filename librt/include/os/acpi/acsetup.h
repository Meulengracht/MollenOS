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
 * MollenOS MCore - ACPI Minimal Subset for drivers
 * - This header enables a minimal subset of ACPI for
 *   driver usage
 */

#if !defined(__ACPI_SETUP__) && !defined(__ACENV_H__)
#define __ACPI_SETUP__

/* ACPI_MACHINE_WIDTH must be specified in an OS- or compiler-dependent header
 * and must be either 32 or 64. 16-bit ACPICA is no longer supported, as of
 * 12/2006. */
#ifndef ACPI_MACHINE_WIDTH
#if defined(i386) || defined(__i386__)
#define ACPI_MACHINE_WIDTH 32
#elif defined(__x86_64__) || defined(amd64) || defined(__amd64__)
#define ACPI_MACHINE_WIDTH 64
#endif
#endif

/* COMPILER_DEPENDENT_UINT64/INT64 - These types are defined in the
 * compiler-dependent header(s) and were introduced because there is no common
 * 64-bit integer type across the various compilation models, as shown in
 * the table below. */
#if defined(_MSC_VER)
#define COMPILER_DEPENDENT_INT64    __int64
#define COMPILER_DEPENDENT_UINT64   unsigned __int64
#define ACPI_INLINE                 __inline

/*
* Calling conventions:
*
* ACPI_SYSTEM_XFACE        - Interfaces to host OS (handlers, threads)
* ACPI_EXTERNAL_XFACE      - External ACPI interfaces
* ACPI_INTERNAL_XFACE      - Internal ACPI interfaces
* ACPI_INTERNAL_VAR_XFACE  - Internal variable-parameter list interfaces
*/
#define ACPI_SYSTEM_XFACE           __cdecl
#define ACPI_EXTERNAL_XFACE
#define ACPI_INTERNAL_XFACE
#define ACPI_INTERNAL_VAR_XFACE     __cdecl

#elif defined(__clang__)
#define COMPILER_DEPENDENT_INT64    long long
#define COMPILER_DEPENDENT_UINT64   unsigned long long
#define ACPI_INLINE                 __inline

/*
* Calling conventions:
*
* ACPI_SYSTEM_XFACE        - Interfaces to host OS (handlers, threads)
* ACPI_EXTERNAL_XFACE      - External ACPI interfaces
* ACPI_INTERNAL_XFACE      - Internal ACPI interfaces
* ACPI_INTERNAL_VAR_XFACE  - Internal variable-parameter list interfaces
*/
#define ACPI_SYSTEM_XFACE           __cdecl
#define ACPI_EXTERNAL_XFACE
#define ACPI_INTERNAL_XFACE
#define ACPI_INTERNAL_VAR_XFACE     __cdecl
#else
#error "Define acpi types for other compilers"
#endif

#endif //!__ACPI_SETUP__

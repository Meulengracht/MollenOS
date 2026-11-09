/**
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

#define __MODULE "ACPI-ARM64"

#include <arch/acpi.h>
#include <debug.h>

oserr_t
ArchAcpiInitialize(void)
{
    // Table discovery alone cannot initialize this platform. ARM MADT entries
    // must describe the CPUs and GIC, GTDT must describe the timer interrupt,
    // and FADT must supply the PSCI conduit before later startup can proceed.
    // Until those consumers are connected, fail here instead of reporting an
    // empty platform as ready and later accessing zero-valued device addresses.
    ERROR("ARM64 ACPI platform discovery is not implemented");
    return OS_ENOTSUPPORTED;
}

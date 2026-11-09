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

#ifndef __VALI_ARCH_ACPI_H__
#define __VALI_ARCH_ACPI_H__

#include <os/osdefs.h>

/**
 * @brief Reads ACPI tables needed to configure this architecture's platform.
 *
 * Kernel memory allocation and mapping must be ready, and AcpiInitializeEarly
 * must have initialized ACPICA's table manager. AcpiAvailable reports table
 * availability before this hook runs. The hook must release each AcpiGetTable
 * reference with AcpiPutTable and publish only validated platform descriptions.
 * Interrupt delivery, timer startup and secondary-CPU startup run later.
 * This routine must not initialize ACPICA again or load the AML namespace.
 *
 * @return OS_EOK when platform discovery succeeded, otherwise an error that
 *         the machine initialization path must handle before starting devices.
 *         Failure does not make the shared table manager unavailable.
 */
__EXTERN oserr_t
ArchAcpiInitialize(void);

#endif //!__VALI_ARCH_ACPI_H__

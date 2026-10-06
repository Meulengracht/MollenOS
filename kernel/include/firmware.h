/**
 * Copyright 2026, Philip Meulengracht
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
 * Firmware table providers (ACPI and DeviceTree)
 */

#ifndef __FIRMWARE_H__
#define __FIRMWARE_H__

#include <os/osdefs.h>
#include <ddk/firmware.h>

/**
 * @brief Fills <infoOut> with the firmware sources known to the kernel.
 */
__EXTERN oserr_t
FirmwareQuery(
    _Out_ OSFirmwareInfo_t* infoOut);

/**
 * @brief Resolves a firmware table to its kernel-resident data. The data
 * remains valid for the lifetime of the system.
 */
__EXTERN oserr_t
FirmwareLocate(
    _In_  const OSFirmwareTableKey_t* key,
    _Out_ const void**                dataOut,
    _Out_ OSFirmwareTable_t*          tableOut);

#endif //!__FIRMWARE_H__

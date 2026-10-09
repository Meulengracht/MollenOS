/**
 * MollenOS
 *
 * Copyright 2015, Philip Meulengracht
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

#ifndef __DEVICED_BUS_LEGACY_FIXED_H__
#define __DEVICED_BUS_LEGACY_FIXED_H__

#include <os/osdefs.h>

// Fixed device-id and vendor-id values for loading non-dynamic devices
#define PCI_FIXED_VENDORID    0xFFEF
#define PCI_CMOS_RTC_DEVICEID 0x0010
#define PCI_PIT_DEVICEID      0x0020
#define PCI_PS2_DEVICEID      0x0030

/**
 * @brief Registers the fixed keyboard and mouse controller on legacy systems.
 * @return OS_EOK on success, or an allocation or I/O registration error.
 */
__EXTERN oserr_t
__InstallPS2Controller(void);

#endif // __DEVICED_BUS_LEGACY_FIXED_H__

/**
 * MollenOS
 *
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

#ifndef __FIRMWARE_ETHERNET_H__
#define __FIRMWARE_ETHERNET_H__

#include <firmware/tree.h>
#include <ddk/ethernet.h>

/**
 * @brief Resolve a MAC's PHY and copy its firmware configuration.
 * The PHY must use Clause 22 and sit directly below the MAC or its GENET MDIO
 * child. MDIO is the management bus used to read and configure the PHY.
 * This call checks firmware wiring, address selection, speed limits and optional
 * reset timing. It does not map registers or communicate with the PHY.
 *
 * @param tree Shared tree. The caller must hold a reference during this call.
 * @param node MAC structure offset. External MDIO and fixed links are unsupported.
 * @param configuration Receives validated configuration. Unchanged on failure.
 * @return OS_EOK, OS_ENOENT for an absent node, or a validation or support error.
 */
oserr_t
FdtEthernetConfiguration(
    _In_  const struct FdtTree*               tree,
    _In_  uint32_t                            node,
    _Out_ struct DeviceEthernetConfiguration* configuration);

#endif //!__FIRMWARE_ETHERNET_H__

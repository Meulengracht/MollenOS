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

#ifndef __ETHERNET_PHY_PRIVATE_H__
#define __ETHERNET_PHY_PRIVATE_H__

#include "phy.h"

// Exact ID1:ID2 values supported by the Broadcom setup below. Keep the revision
// bits: other revisions have not been checked for the same shadow registers.
#define PHY_BCM54210E  0x600d84a0U
#define PHY_BCM54213PE 0x600d84a2U

/**
 * @brief Configure the identified BCM54210E/BCM54213PE for polling and RGMII.
 * @param phy Reset PHY of one of the exact supported IDs.
 * @return OS_EOK after delay, power-down and EEE readback checks, an MDIO error,
 *         or OS_EUNKNOWN if a checked register did not accept its new bits.
 */
__EXTERN oserr_t
PhyBroadcomInitialize(
    _In_ PhyDevice_t* phy);

#endif //!__ETHERNET_PHY_PRIVATE_H__

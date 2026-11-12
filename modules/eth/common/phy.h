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

#ifndef __ETHERNET_PHY_H__
#define __ETHERNET_PHY_H__

#include <ddk/ethernet.h>

#include "mdio.h"

// Clause 22 register numbers. ID1 and ID2 form the 32-bit PHY identifier.
#define PHY_BMCR       0
#define PHY_BMSR       1
#define PHY_ID1        2
#define PHY_ID2        3
#define PHY_ADVERTISE  4
#define PHY_PARTNER    5
#define PHY_CTRL1000   9
#define PHY_STAT1000   10
#define PHY_EXTSTATUS  15

// Basic mode control (BMCR): reset, isolate, and restart autonegotiation.
#define PHY_RESET      0x8000U
#define PHY_ISOLATE    0x0400U
#define PHY_AUTONEG    0x1000U
#define PHY_RESTART    0x0200U

// Basic mode status (BMSR): link, negotiation and supported copper modes.
#define PHY_LINK         0x0004U
#define PHY_AN_COMPLETE  0x0020U
#define PHY_AN_CAPABLE   0x0008U
#define PHY_REMOTE_FAULT 0x0010U
#define PHY_EXT_STATUS   0x0100U
#define PHY_CAP_10_HALF  0x0800U
#define PHY_CAP_10_FULL  0x1000U
#define PHY_CAP_100_HALF 0x2000U
#define PHY_CAP_100_FULL 0x4000U

// Local and partner advertisement use the same 10/100 mode bit positions.
#define PHY_ADV_802_3    0x0001U
#define PHY_ADV_10_HALF  0x0020U
#define PHY_ADV_10_FULL  0x0040U
#define PHY_ADV_100_HALF 0x0080U
#define PHY_ADV_100_FULL 0x0100U
#define PHY_ADV_MODES    0x01e0U
#define PHY_ADV_FAULT    0x2000U

// Extended status identifies 1000BASE-T support. CTRL1000 advertises it.
#define PHY_CAP_1000_HALF 0x1000U
#define PHY_CAP_1000_FULL 0x2000U
#define PHY_ADV_1000_FULL 0x0200U

// CTRL1000 bits 12:8: forced master setting, port type and both duplex modes.
#define PHY_CTRL1000_MODES 0x1f00U

// STAT1000 bit 15 reports failed master/slave resolution.
#define PHY_1000_FAULT     0x8000U

// Poll reset for 50 intervals of 10 ms, plus the time spent on MDIO reads.
#define PHY_RESET_ATTEMPTS 50U
#define PHY_RESET_DELAY_US 10000U

/**
 * @brief Copper link speed in Mbit/s, duplex and carrier state.
 * All fields are zero when the link is down or negotiation is incomplete.
 */
struct PhyLink {
    uint32_t Speed;
    int      FullDuplex;
    int      Carrier;
};

/**
 * @brief Identified PHY and the capabilities actually advertised to its partner.
 * Ready is set only after reset, model-specific setup and negotiation restart.
 * The MAC serializes calls and applies PhyLink to its own registers.
 */
typedef struct PhyDevice {
    MdioBus_t* Bus;
    uint32_t   Id;
    uint32_t   Mode;
    uint32_t   MaxSpeed;
    uint16_t   Advertisement;
    uint16_t   Gigabit;
    int        HasGigabit;
    int        Ready;
#ifdef VALI_ETHERNET_DIAGNOSTICS
    // Last observed BMSR suppresses unchanged negotiation messages.
    uint16_t   LastStatus;
    int        StatusLogged;
#endif
} PhyDevice_t;

/**
 * @brief Identify, reset, configure delays and start copper autonegotiation.
 * @param phy Zero-initialized state, or a stopped PHY being initialized again.
 * @param bus Initialized MDIO endpoint with a single executor.
 * @param configuration Firmware configuration for that same endpoint.
 * @return OS_EOK once negotiation starts. Unknown RGMII PHYs return
 *         OS_ENOTSUPPORTED because generic negotiation cannot establish delays.
 */
__EXTERN oserr_t
PhyInitialize(
    _InOut_ PhyDevice_t*                              phy,
    _In_    MdioBus_t*                                bus,
    _In_    const struct DeviceEthernetConfiguration* configuration);

/**
 * @brief Read current negotiated speed and duplex without changing MAC state.
 * @param phy Initialized PHY, serialized with all other MDIO accesses.
 * @param link Complete result. Cleared on error so stale carrier cannot persist.
 * @return OS_EOK for a resolved link, link down, unfinished negotiation, a
 *         reported negotiation fault or no common mode. OS_EBUSY if setup has
 *         not completed or the PHY is stopped. Otherwise the MDIO error.
 */
__EXTERN oserr_t
PhyReadLink(
    _In_  PhyDevice_t*    phy,
    _Out_ struct PhyLink* link);

/**
 * @brief Isolate the PHY after the MAC has stopped using it.
 * @param phy State with an initialized Bus, serialized with polling. May be
 *            called after failed initialization or retried after failed isolation.
 * @return MDIO status. Ready is cleared even when the write fails.
 */
__EXTERN oserr_t
PhyStop(
    _InOut_ PhyDevice_t* phy);

#endif //!__ETHERNET_PHY_H__

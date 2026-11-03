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

#ifndef __DDK_ETHERNET_H__
#define __DDK_ETHERNET_H__

#include <ddk/ddkdefs.h>

_CODE_BEGIN

enum DeviceEthernetMode {
    // MII. Four data lines in each direction, clocked by the PHY at 2.5MHz for
    // 10Mbit/s and 25MHz for 100Mbit/s. Supports 10 and 100Mbit/s only.
    DeviceEthernetMii = 1,

    // GMII. Eight data lines in each direction, clocked at 125MHz for 1Gbit/s.
    // Falls back to the MII lines and clocks for 10 and 100Mbit/s.
    DeviceEthernetGmii,

    // RMII. A reduced version of MII that uses two data lines in each direction
    // and a shared 50MHz reference clock, which saves pins on the board.
    // Supports 10 and 100Mbit/s only.
    DeviceEthernetRmii,

    // RGMII. A reduced version of GMII that uses four data lines in each
    // direction and samples them on both clock edges. Supports 10, 100 and
    // 1000Mbit/s with clocks of 2.5, 25 and 125MHz respectively. The sampling
    // clock must arrive after the data has settled, typically by about 2ns,
    // in each direction. This mode requests no internal PHY delays, so the
    // board wiring or MAC must supply the required clock-to-data timing.
    DeviceEthernetRgmii,

    // RGMII-ID. The PHY inserts both the receive and the transmit clock delay.
    // Receive runs from PHY to MAC, and transmit runs from MAC to PHY. The MAC
    // must not add the same delays again.
    DeviceEthernetRgmiiId,

    // RGMII-RXID. The PHY inserts only the receive clock delay, so the MAC must
    // not add a receive delay. The board determines how to supply the transmit
    // delay. This mode does not specify it.
    DeviceEthernetRgmiiRxId,

    // RGMII-TXID. The PHY inserts only the transmit clock delay, so the MAC must
    // not add a transmit delay. The board determines how to supply the receive
    // delay. This mode does not specify it.
    DeviceEthernetRgmiiTxId
};

/**
 * @brief PHY reset GPIO and timing in microseconds.
 * GpioPresent indicates whether a reset line is configured, since GPIO zero is
 * a valid pin. Controller is a firmware node offset, not a device ID or register
 * address. Bit zero of Flags indicates active low. Before using MDIO, the MAC
 * requests a PHY reset through deviced.
 */
struct DeviceEthernetReset {
    uint32_t GpioPresent;
    uint32_t Controller;
    uint32_t Pin;
    uint32_t Flags;
    uint32_t AssertUs;
    uint32_t DeassertUs;
};

/**
 * @brief Firmware configuration copied for a MAC's Clause 22 MDIO PHY.
 * PhyNode and MdioNode are firmware offsets for diagnostics. MdioNode equals
 * the MAC node when PHY children are directly below the MAC. MdioOffset and
 * MdioLength describe a separate MDIO child within the MAC's first register
 * range. Both are zero for integrated MDIO.
 *
 * Mode specifies the MAC to PHY interface. For RGMII-ID, RGMII-RXID and
 * RGMII-TXID, it also specifies the delays the PHY must insert. The MAC must
 * not add those delays again. Plain RGMII requests no internal PHY delays.
 * MaxSpeed is in Mbit/s. Zero means there is no firmware speed limit.
 * MacPresent is zero if the firmware MAC address is absent or invalid,
 * including the zero placeholder in bundled trees.
 *
 * Before using MDIO, query this structure, call DevicePrepare and reset the
 * PHY. DevicePrepare handles generic clocks, power, resets and pins. The
 * driver identifies the PHY and programs the MAC and PHY delays before
 * starting link negotiation.
 */
struct DeviceEthernetConfiguration {
    uint32_t Version;
    uint32_t PhyNode;
    uint32_t MdioNode;
    uint32_t PhyAddress;
    uint32_t MdioOffset;
    uint32_t MdioLength;
    uint32_t Mode;
    uint32_t MaxSpeed;
    uint32_t MacPresent;
    uint8_t  Mac[6];
    uint8_t  Reserved[2];
    
    struct DeviceEthernetReset Reset;
};

/**
 * @brief Query the registered MAC's firmware configuration before DevicePrepare.
 *
 * @param device Registered native or RP1 Ethernet device.
 * @param configuration Receives version 1 configuration. Unchanged on failure.
 * @return OS_EOK on success, or a firmware, transport or unsupported-topology error.
 */
DDKDECL(oserr_t,
DeviceGetEthernetConfiguration(
    _In_  uuid_t                              device,
    _Out_ struct DeviceEthernetConfiguration* configuration));

/**
 * @brief Pulse the configured PHY reset after DevicePrepare and before MDIO use.
 *
 * @param device Prepared Ethernet device. Stop the MAC before requesting reset.
 * @return OS_EOK on success or if no reset GPIO is configured. Otherwise, a setup
 *         or register-access error.
 */
DDKDECL(oserr_t,
DeviceResetEthernetPhy(
    _In_ uuid_t device));

_CODE_END

#endif

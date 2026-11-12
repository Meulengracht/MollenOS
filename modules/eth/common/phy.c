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

#include <ddk/utils.h>
#include <string.h>

#include "phy-private.h"
#include "diagnostics.h"

static oserr_t
__PhyIdentify(
    _In_  MdioBus_t* bus,
    _Out_ uint32_t*  identity)
{
    uint16_t high;
    uint16_t low;
    oserr_t  status;

    status = MdioRead(bus, PHY_ID1, &high);
    if (status != OS_EOK) {
        return status;
    }

    status = MdioRead(bus, PHY_ID2, &low);
    if (status != OS_EOK) {
        return status;
    }

    // ID1 supplies the upper 16 bits. Empty or unresponsive bus addresses may
    // return all zeroes or all ones instead of a device identifier.
    *identity = ((uint32_t)high << 16) | low;
    if (*identity == 0 || *identity == UINT32_MAX || high == 0xffff) {
        return OS_ENOENT;
    }
    return OS_EOK;
}

static oserr_t
__PhyReset(
    _In_ MdioBus_t* bus)
{
    uint32_t attempts;
    uint16_t control;
    oserr_t  status;

    status = MdioWrite(bus, PHY_BMCR, PHY_RESET);
    if (status != OS_EOK) {
        return status;
    }

    // BMCR reset clears itself. Delay before each read so initialization does
    // not occupy the bus continuously while the PHY resets.
    for (attempts = 0; attempts < PHY_RESET_ATTEMPTS; attempts++) {
        status = bus->Operations->DelayUs(bus->Context, PHY_RESET_DELAY_US);
        if (status != OS_EOK) {
            ERROR("__PhyReset: address %u delay failed (%u)", bus->Address, status);
            return status;
        }

        status = MdioRead(bus, PHY_BMCR, &control);
        if (status != OS_EOK) {
            return status;
        }

        if (!(control & PHY_RESET)) {
            return OS_EOK;
        }
    }
    ERROR("__PhyReset: address %u reset bit still set after 500 ms", bus->Address);
    return OS_ETIMEOUT;
}

static oserr_t
__PhyNegotiate(
    _InOut_ PhyDevice_t* phy)
{
    uint16_t abilities;
    uint16_t extended = 0;
    uint16_t advertise = PHY_ADV_802_3;
    uint16_t gigabit = 0;
    uint32_t limit = phy->MaxSpeed ? phy->MaxSpeed : 1000;
    oserr_t  status;

    status = MdioRead(phy->Bus, PHY_BMSR, &abilities);
    if (status != OS_EOK) {
        return status;
    }

    if (!(abilities & PHY_AN_CAPABLE)) {
        return OS_ENOTSUPPORTED;
    }

    // Advertise only modes supported by both this PHY and the MAC. Pause and
    // 1000BASE-T half duplex are unsupported. The selector stays IEEE 802.3.
    if (abilities & PHY_CAP_10_HALF) {
        advertise |= PHY_ADV_10_HALF;
    }

    if (abilities & PHY_CAP_10_FULL) {
        advertise |= PHY_ADV_10_FULL;
    }

    if (limit >= 100) {
        if (abilities & PHY_CAP_100_HALF) {
            advertise |= PHY_ADV_100_HALF;
        }
        if (abilities & PHY_CAP_100_FULL) {
            advertise |= PHY_ADV_100_FULL;
        }
    }

    if (abilities & PHY_EXT_STATUS) {
        status = MdioRead(phy->Bus, PHY_EXTSTATUS, &extended);
        if (status != OS_EOK) {
            return status;
        }
    }

    phy->HasGigabit = (extended & (PHY_CAP_1000_HALF | PHY_CAP_1000_FULL)) != 0;
    if (limit >= 1000 && (extended & PHY_CAP_1000_FULL)) {
        gigabit = PHY_ADV_1000_FULL;
    }

    if (!(advertise & PHY_ADV_MODES) && !gigabit) {
        return OS_ENOTSUPPORTED;
    }

    status = MdioWrite(phy->Bus, PHY_ADVERTISE, advertise);
    if (status != OS_EOK) {
        return status;
    }

    if (phy->HasGigabit) {
        // Clear forced master/slave selection left by firmware as well as
        // unsupported gigabit advertisements. Let the endpoints resolve it.
        status = MdioModify(phy->Bus, PHY_CTRL1000, PHY_CTRL1000_MODES, gigabit);
        if (status != OS_EOK) {
            return status;
        }
    }

    // Write the restart bit even if it reads as set. Negotiation completes
    // asynchronously, so Ready below only means that setup has finished.
    status = MdioWrite(phy->Bus, PHY_BMCR, PHY_AUTONEG | PHY_RESTART);
    if (status != OS_EOK) {
        return status;
    }

    phy->Advertisement = advertise;
    phy->Gigabit = gigabit;

    ETHERNET_LOG("PHY autoneg started: address=%u max-speed=%u ANAR=0x%04x CTRL1000=0x%04x pause=off",
        phy->Bus->Address, limit, advertise, gigabit);
    return OS_EOK;
}

oserr_t
PhyInitialize(
    _InOut_ PhyDevice_t*                              phy,
    _In_    MdioBus_t*                                bus,
    _In_    const struct DeviceEthernetConfiguration* configuration)
{
    uint32_t identity;
    oserr_t  status;

    phy->Ready = 0;

#ifdef VALI_ETHERNET_DIAGNOSTICS
    phy->StatusLogged = 0;
#endif

    phy->Bus = bus;
    phy->Mode = configuration->Mode;
    phy->MaxSpeed = configuration->MaxSpeed;

    // Version 1 supplies the interface mode, PHY address and speed limit.
    // Only copper 10/100/1000 modes are implemented. Zero means no firmware cap.
    if (configuration->Version != 1 || configuration->PhyAddress != bus->Address) {
        return OS_EINVALPARAMS;
    }

    if (phy->Mode < DeviceEthernetMii || phy->Mode > DeviceEthernetRgmiiTxId) {
        return OS_ENOTSUPPORTED;
    }

    if (phy->MaxSpeed && phy->MaxSpeed != 10 && phy->MaxSpeed != 100 && phy->MaxSpeed != 1000) {
        return OS_ENOTSUPPORTED;
    }

    // MII and RMII cannot carry a gigabit link, even if the PHY supports it.
    if ((phy->Mode == DeviceEthernetMii || phy->Mode == DeviceEthernetRmii) &&
        (!phy->MaxSpeed || phy->MaxSpeed > 100)) {
        phy->MaxSpeed = 100;
    }

    status = __PhyIdentify(bus, &identity);
    if (status != OS_EOK) {
        ERROR("PhyInitialize: address %u identification failed (%u)", bus->Address, status);
        return status;
    }

    phy->Id = identity;

    ETHERNET_LOG("PHY identify: address=%u id=0x%08x model=%s", bus->Address, identity,
        identity == PHY_BCM54213PE ? "BCM54213PE" :
        identity == PHY_BCM54210E ? "BCM54210E" : "unknown");

    // RGMII requires model-specific delay setup. A generic PHY must not be
    // accepted when we cannot verify those delay registers.
    if (phy->Mode >= DeviceEthernetRgmii &&
        identity != PHY_BCM54210E && identity != PHY_BCM54213PE) {
        ERROR("PhyInitialize: unsupported RGMII PHY 0x%08x at address %u", identity, bus->Address);
        return OS_ENOTSUPPORTED;
    }

    ETHERNET_LOG("PHY software reset: begin address=%u", bus->Address);

    status = __PhyReset(bus);
    if (status != OS_EOK) {
        ERROR("PhyInitialize: address %u reset failed (%u)", bus->Address, status);
        return status;
    }

    status = __PhyIdentify(bus, &identity);
    if (status != OS_EOK || identity != phy->Id) {
        ERROR("PhyInitialize: address %u identity check after reset failed (%u)",
            bus->Address, status == OS_EOK ? OS_EPROTOCOL : status);
        return status == OS_EOK ? OS_EPROTOCOL : status;
    }

    ETHERNET_LOG("PHY software reset: complete id=0x%08x", identity);
    if (identity == PHY_BCM54210E || identity == PHY_BCM54213PE) {
        status = PhyBroadcomInitialize(phy);
        if (status != OS_EOK) {
            ERROR("PhyInitialize: address %u Broadcom setup failed (%u)", bus->Address, status);
            return status;
        }
    }

    status = __PhyNegotiate(phy);
    if (status == OS_EOK) {
        phy->Ready = 1;
    } else {
        ERROR("PhyInitialize: address %u negotiation setup failed (%u)", bus->Address, status);
    }
    return status;
}

oserr_t
PhyReadLink(
    _In_  PhyDevice_t*    phy,
    _Out_ struct PhyLink* link)
{
    uint16_t bmsr;
    uint16_t partner;
    uint16_t gigabit = 0;
    uint16_t common;
    oserr_t  status;

    // Build the result only from a complete set of reads. Any failed read or
    // unfinished negotiation leaves speed, duplex and carrier cleared.
    memset(link, 0, sizeof(struct PhyLink));

    if (!phy->Ready) {
        return OS_EBUSY;
    }

    // BMSR link is latched low. The second read reports the current level.
    status = MdioRead(phy->Bus, PHY_BMSR, &bmsr);
    if (status != OS_EOK) {
        return status;
    }

    status = MdioRead(phy->Bus, PHY_BMSR, &bmsr);
    if (status != OS_EOK) {
        return status;
    }

#ifdef VALI_ETHERNET_DIAGNOSTICS
    if (!phy->StatusLogged || phy->LastStatus != bmsr) {
        ETHERNET_LOG("PHY negotiation: BMSR=0x%04x link=%u complete=%u remote-fault=%u",
            bmsr, !!(bmsr & PHY_LINK), !!(bmsr & PHY_AN_COMPLETE), !!(bmsr & PHY_REMOTE_FAULT));
        phy->LastStatus = bmsr;
        phy->StatusLogged = 1;
    }
#endif

    if ((bmsr & (PHY_LINK | PHY_AN_COMPLETE)) != (PHY_LINK | PHY_AN_COMPLETE)) {
        return OS_EOK;
    }
    if (bmsr & PHY_REMOTE_FAULT) {
        return OS_EOK;
    }

    status = MdioRead(phy->Bus, PHY_PARTNER, &partner);
    if (status != OS_EOK) {
        return status;
    }

    if (partner & PHY_ADV_FAULT) {
        return OS_EOK;
    }

    if (phy->HasGigabit) {
        status = MdioRead(phy->Bus, PHY_STAT1000, &gigabit);
        if (status != OS_EOK) {
            return status;
        }

        if (gigabit & PHY_1000_FAULT) {
            return OS_EOK;
        }
    }

    // STAT1000 puts the partner's full-duplex bit at bit 11. Shift it to bit 9
    // to compare with our CTRL1000 advertisement. Prefer the fastest common mode,
    // then full duplex at that speed.
    if ((gigabit >> 2) & phy->Gigabit & PHY_ADV_1000_FULL) {
        link->Speed = 1000;
        link->FullDuplex = 1;
    } else {
        common = partner & phy->Advertisement;
        if (common & PHY_ADV_100_FULL) {
            link->Speed = 100;
            link->FullDuplex = 1;
        } else if (common & PHY_ADV_100_HALF) {
            link->Speed = 100;
        } else if (common & PHY_ADV_10_FULL) {
            link->Speed = 10;
            link->FullDuplex = 1;
        } else if (common & PHY_ADV_10_HALF) {
            link->Speed = 10;
        } else {
            return OS_EOK;
        }
    }

    link->Carrier = 1;
    return OS_EOK;
}

oserr_t
PhyStop(
    _InOut_ PhyDevice_t* phy)
{
    // Prevent further polling even if isolation fails. The MAC must already
    // be stopped because PHY isolation does not stop descriptor DMA.
    phy->Ready = 0;
    return MdioModify(phy->Bus, PHY_BMCR, PHY_ISOLATE, PHY_ISOLATE);
}

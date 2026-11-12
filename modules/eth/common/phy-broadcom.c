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

#include "phy-private.h"
#include "diagnostics.h"
#include <ddk/utils.h>

// Broadcom Clause 22 register numbers.
#define BCM_EXT_CONTROL  0x10
#define BCM_AUX_CONTROL  0x18
#define BCM_INT_STATUS   0x1a
#define BCM_INT_MASK     0x1b
#define BCM_SHADOW       0x1c

// Register 0x1c: write bit 15, shadow selector bits 14:10, data bits 9:0.
#define BCM_SHADOW_WRITE  0x8000U
#define BCM_SHADOW_SHIFT  10U
#define BCM_SHADOW_DATA   0x03ffU
#define BCM_SHADOW_CLOCK  3U
#define BCM_SHADOW_SPARE3 5U
#define BCM_SHADOW_APD    10U

// Register 0x18: read selector bits 14:12, write selector bits 2:0.
// Reading any shadow requires selector 7 in the low bits.
#define BCM_AUX_SELECT_MASK 7U
#define BCM_AUX_READ_SHIFT  12U
#define BCM_AUX_SELECT      0U
#define BCM_MISC_SELECT     7U
#define BCM_MISC_WRITE      0x8000U
#define BCM_RX_DELAY        0x0100U
#define BCM_TX_DELAY        0x0200U
#define BCM_TX_AMPLITUDE    0x0400U
#define BCM_DSP_CLOCK       0x0800U

// Extended control, spare control 3 and automatic power-down fields.
#define BCM_INT_DISABLE     0x1000U
#define BCM_INT_FORCE       0x0800U
#define BCM_DLL_APD_DISABLE 0x0002U
#define BCM_APD_ENABLE      0x0020U

// Clause 22 indirect MMD access. Device 7 contains autonegotiation registers.
#define BCM_MMD_CONTROL   13U
#define BCM_MMD_DATA      14U
#define BCM_MMD_AN        7U
#define BCM_MMD_DATA_MODE 0x4000U
#define BCM_MMD_EEE_ADV   0x003cU

static oserr_t
__BroadcomShadowRead(
    _In_  MdioBus_t* bus,
    _In_  uint16_t   shadow,
    _Out_ uint16_t*  value)
{
    uint16_t result;
    oserr_t  status;

    // The caller serializes selection and read together. Clear the
    // write bit when selecting, then remove the selector from the result.
    status = MdioWrite(bus, BCM_SHADOW, shadow << BCM_SHADOW_SHIFT);
    if (status != OS_EOK) {
        return status;
    }

    status = MdioRead(bus, BCM_SHADOW, &result);
    if (status == OS_EOK) {
        *value = result & BCM_SHADOW_DATA;
    }
    return status;
}

static oserr_t
__BroadcomShadowModify(
    _In_ MdioBus_t* bus,
    _In_ uint16_t   shadow,
    _In_ uint16_t   mask,
    _In_ uint16_t   bits)
{
    uint16_t value;
    uint16_t readback;
    oserr_t  status;

    status = __BroadcomShadowRead(bus, shadow, &value);
    if (status != OS_EOK) {
        return status;
    }

    // Preserve the other data bits and include write-enable with the selector.
    value = (value & ~mask) | (bits & mask);
    status = MdioWrite(bus, BCM_SHADOW, BCM_SHADOW_WRITE | (shadow << BCM_SHADOW_SHIFT) | value);
    if (status != OS_EOK) {
        return status;
    }

    status = __BroadcomShadowRead(bus, shadow, &readback);
    if (status != OS_EOK) {
        return status;
    }
    if ((readback & mask) != (value & mask)) {
        ERROR("__BroadcomShadowModify: address %u shadow %u mask 0x%04x expected 0x%04x read 0x%04x",
            bus->Address, shadow, mask, value & mask, readback & mask);
        return OS_EUNKNOWN;
    }
    return OS_EOK;
}

static oserr_t
__BroadcomAuxRead(
    _In_  MdioBus_t* bus,
    _In_  uint16_t   shadow,
    _Out_ uint16_t*  value)
{
    oserr_t status;

    // AUX uses different fields for read and write selection. Low bits 111
    // select a read operation, while bits 14:12 choose the requested shadow.
    status = MdioWrite(bus, BCM_AUX_CONTROL, BCM_AUX_SELECT_MASK | (shadow << BCM_AUX_READ_SHIFT));
    if (status != OS_EOK) {
        return status;
    }
    return MdioRead(bus, BCM_AUX_CONTROL, value);
}

static oserr_t
__BroadcomDelays(
    _In_ PhyDevice_t* phy)
{
    uint16_t value;
    uint16_t readback;
    uint16_t receive = 0;
    uint16_t transmit = 0;
    oserr_t  status;

    // Firmware's interface mode specifies which delays the PHY must insert.
    // Plain RGMII leaves both disabled because the board or MAC supplies them.
    if (phy->Mode == DeviceEthernetRgmiiId || phy->Mode == DeviceEthernetRgmiiRxId) {
        receive = BCM_RX_DELAY;
    }

    if (phy->Mode == DeviceEthernetRgmiiId || phy->Mode == DeviceEthernetRgmiiTxId) {
        transmit = BCM_TX_DELAY;
    }

    status = __BroadcomAuxRead(phy->Bus, BCM_MISC_SELECT, &value);
    if (status != OS_EOK) {
        return status;
    }

    // AUX shadow 7 requires its write-enable bit. Preserve the other fields,
    // including automatic crossover. PHY RX and TX delays are independent.
    value = (value & ~BCM_RX_DELAY) | receive | BCM_MISC_WRITE | BCM_MISC_SELECT;
    status = MdioWrite(phy->Bus, BCM_AUX_CONTROL, value);
    if (status != OS_EOK) {
        return status;
    }

    status = __BroadcomAuxRead(phy->Bus, BCM_MISC_SELECT, &readback);
    if (status != OS_EOK) {
        return status;
    }

    if ((readback & BCM_RX_DELAY) != receive) {
        ERROR("__BroadcomDelays: address %u AUX shadow 7 RX delay expected 0x%04x read 0x%04x",
            phy->Bus->Address, receive, readback & BCM_RX_DELAY);
        return OS_EUNKNOWN;
    }
    // TX delay is bit 9 of clock-alignment shadow 3 in register 0x1c.
    status = __BroadcomShadowModify(phy->Bus, BCM_SHADOW_CLOCK, BCM_TX_DELAY, transmit);
    if (status == OS_EOK) {
        ETHERNET_LOG("PHY RGMII delays verified: RX=%s TX=%s AUX7=0x%04x TX-shadow3-mask=0x%04x",
            receive ? "on" : "off", transmit ? "on" : "off", readback, transmit);
    }
    return status;
}

static oserr_t
__BroadcomDisableEee(
    _In_ MdioBus_t* bus)
{
    uint16_t value;
    oserr_t  status;

    // Clause 22 MMD access to AN device 7, EEE advertisement register 0x3c.
    // The MAC has no low-power-idle support, so do not negotiate EEE.
    // First select address mode and write the target register number.
    status = MdioWrite(bus, BCM_MMD_CONTROL, BCM_MMD_AN);
    if (status != OS_EOK) {
        return status;
    }

    status = MdioWrite(bus, BCM_MMD_DATA, BCM_MMD_EEE_ADV);
    if (status != OS_EOK) {
        return status;
    }

    // Bit 14 selects data access without incrementing the address. Readback
    // below must inspect the same EEE advertisement register that we write.
    status = MdioWrite(bus, BCM_MMD_CONTROL, BCM_MMD_DATA_MODE | BCM_MMD_AN);
    if (status != OS_EOK) {
        return status;
    }

    // Zero advertises no EEE modes at any supported link speed.
    status = MdioWrite(bus, BCM_MMD_DATA, 0);
    if (status != OS_EOK) {
        return status;
    }

    status = MdioRead(bus, BCM_MMD_DATA, &value);
    if (status != OS_EOK) {
        return status;
    }
    if (value != 0) {
        ERROR("__BroadcomDisableEee: address %u EEE advertisement expected 0 read 0x%04x",
            bus->Address, value);
        return OS_EUNKNOWN;
    }
    return OS_EOK;
}

oserr_t
PhyBroadcomInitialize(
    _In_ PhyDevice_t* phy)
{
    uint16_t value;
    oserr_t  status;

    // BCM54210E and BCM54213PE share these shadows. Keep interrupts masked
    // while using polling and clear any interrupt left by firmware. Extended
    // control bit 12 masks output, while clearing bit 11 disables forced IRQs.
    status = MdioModify(phy->Bus, BCM_EXT_CONTROL, BCM_INT_DISABLE | BCM_INT_FORCE, BCM_INT_DISABLE);
    if (status != OS_EOK) {
        return status;
    }

    // Each set mask bit disables an interrupt source. Status is read to clear.
    status = MdioWrite(phy->Bus, BCM_INT_MASK, UINT16_MAX);
    if (status != OS_EOK) {
        return status;
    }

    status = MdioRead(phy->Bus, BCM_INT_STATUS, &value);
    if (status != OS_EOK) {
        return status;
    }

    // Disable DLL automatic power-down in spare control 3. Automatic PHY
    // power-down and EEE also stay disabled until the MAC supports those states.
    status = __BroadcomShadowModify(
        phy->Bus,
        BCM_SHADOW_SPARE3,
        BCM_DLL_APD_DISABLE,
        BCM_DLL_APD_DISABLE
    );
    if (status != OS_EOK) {
        return status;
    }

    status = __BroadcomShadowModify(phy->Bus, BCM_SHADOW_APD, BCM_APD_ENABLE, 0);
    if (status != OS_EOK) {
        return status;
    }

    status = __BroadcomDelays(phy);
    if (status != OS_EOK) {
        return status;
    }

    // AUX shadow 0 bit 10 enables transmit amplitude control. Apply it with
    // the DSP clock (bit 11) running, then stop that clock before negotiation.
    status = __BroadcomAuxRead(phy->Bus, BCM_AUX_SELECT, &value);
    if (status != OS_EOK) {
        return status;
    }

    // Clear the read selector so both writes target AUX shadow 0.
    value = (value & ~BCM_AUX_SELECT_MASK) | BCM_TX_AMPLITUDE;
    status = MdioWrite(phy->Bus, BCM_AUX_CONTROL, value | BCM_DSP_CLOCK);
    if (status != OS_EOK) {
        return status;
    }

    status = MdioWrite(phy->Bus, BCM_AUX_CONTROL, value & ~BCM_DSP_CLOCK);
    if (status != OS_EOK) {
        return status;
    }

    status = __BroadcomDisableEee(phy->Bus);
    if (status == OS_EOK) {
        ETHERNET_LOG("PHY Broadcom setup complete: TX amplitude control enabled, EEE advertisement verified off");
    }
    return status;
}

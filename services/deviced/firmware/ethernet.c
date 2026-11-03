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

#include <firmware/ethernet.h>
#include <string.h>

struct __EthernetChildren {
    uint32_t MacNode;
    int      FixedLink;
};

static void
__EthernetInspectChild(
    _In_    const struct FdtResources* nodes,
    _In_    int                        depth,
    _InOut_ void*                      context)
{
    struct __EthernetChildren* children = context;

    // A fixed-link must be directly below this MAC. The visitor also reaches
    // deeper nodes, whose fixed-link descriptions belong to other devices.
    if (depth > 0 && nodes[depth - 1].NodeOffset == children->MacNode &&
        !strcmp(nodes[depth].Name, "fixed-link")) {
        children->FixedLink = 1;
    }
}

static oserr_t
__EthernetOptionalScalar(
    _In_  const struct FdtNode* node,
    _In_  const char*           name,
    _Out_ uint32_t*             value)
{
    oserr_t status;

    // The caller must set the default value before calling. Keep it if the
    // property is absent. Invalid property data still returns an error.
    status = FdtScalar(node, name, value);
    return status == OS_ENOENT ? OS_EOK : status;
}

static oserr_t
__EthernetSpeed(
    _In_  const struct FdtNode* node,
    _Out_ uint32_t*             speed)
{
    oserr_t status;

    status = FdtScalar(node, "max-speed", speed);
    if (status == OS_ENOENT) {
        // Zero in the caller's configuration means no firmware speed limit.
        return OS_EOK;
    }

    if (status != OS_EOK) {
        return status;
    }

    // This configuration supports 10, 100 and 1000 Mbit/s. Reject other speeds.
    if (*speed != 10 && *speed != 100 && *speed != 1000) {
        return OS_ENOTSUPPORTED;
    }
    return OS_EOK;
}

static oserr_t
__EthernetMode(
    _In_  const struct FdtNode* node,
    _Out_ uint32_t*             mode)
{
    static const char* const names[] = {
        "mii", "gmii", "rmii", "rgmii", "rgmii-id", "rgmii-rxid", "rgmii-txid"
    };

    const uint8_t* value;
    const uint8_t* alternate;
    uint32_t       length;
    uint32_t       alternateLength;

    // Both properties describe the same wiring. If both are present, their
    // values must match before we can use the configuration.
    value = FdtProperty(node, "phy-mode", &length);
    if (value == NULL) {
        value = FdtProperty(node, "phy-connection-type", &length);
    }

    alternate = FdtProperty(node, "phy-connection-type", &alternateLength);
    if (alternate != NULL && value != NULL) {
        if (length != alternateLength || memcmp(value, alternate, length)) {
            return OS_EINVALPARAMS;
        }
    }

    if (value == NULL || length < 2 || value[length - 1] != 0) {
        return OS_EINVALPARAMS;
    }

    // The property must contain exactly one supported mode, including its
    // terminator. A matching prefix followed by another string is invalid.
    // This table follows the public enum order, starting at DeviceEthernetMii.
    for (size_t i = 0; i < SIZEOF_ARRAY(names); i++) {
        if (length == strlen(names[i]) + 1 && !memcmp(value, names[i], length)) {
            *mode = (uint32_t)i + DeviceEthernetMii;
            return OS_EOK;
        }
    }
    return OS_ENOTSUPPORTED;
}

static oserr_t
__EthernetMac(
    _In_    const struct FdtNode*               node,
    _InOut_ struct DeviceEthernetConfiguration* config)
{
    static const char* const names[] = { "mac-address", "local-mac-address", "address" };
    
    uint32_t length;
    uint8_t  nonzero;

    // Try the firmware address before the board default. Skip zero placeholders
    // and multicast addresses, since neither can be used as a station address.
    for (unsigned int i = 0; i < SIZEOF_ARRAY(names); i++) {
        const uint8_t* value = FdtProperty(node, names[i], &length);
        if (value == NULL) {
            continue;
        }

        if (length != sizeof(config->Mac)) {
            return OS_EINVALPARAMS;
        }
        
        // A station address must be nonzero with bit zero of the first byte
        // clear. That bit indicates a multicast address.
        nonzero = 0;
        for (unsigned int j = 0; j < length; j++) {
            nonzero |= value[j];
        }

        if (nonzero && !(value[0] & 1)) {
            memcpy(config->Mac, value, sizeof(config->Mac));
            config->MacPresent = 1;
            break;
        }
    }
    return OS_EOK;
}

static oserr_t
__EthernetReset(
    _In_  const struct FdtTree*       tree,
    _In_  const struct FdtNode*       mac,
    _In_  const struct FdtNode*       phy,
    _Out_ struct DeviceEthernetReset* reset)
{
    struct FdtResources gpio;
    const uint8_t*      line;
    const uint8_t*      legacy;
    uint32_t            length;
    uint32_t            legacyLength;
    uint32_t            cells;
    uint32_t            duration = 0;
    oserr_t             status;

    // New bindings put reset on the PHY. Older bindings put it on the MAC.
    // Accept at most one description, even if both properties name the same line.
    line = FdtProperty(phy, "reset-gpios", &length);
    legacy = FdtProperty(mac, "phy-reset-gpios", &legacyLength);
    if (line != NULL && legacy != NULL) {
        return OS_ENOTSUPPORTED;
    }

    if (legacy != NULL) {
        line = legacy;
        length = legacyLength;
    }

    if (line != NULL) {
        // Support exactly one controller reference, pin number and flag cell.
        // Other GPIO encodings need controller-specific decoding before use.
        if (length != 12) {
            return OS_ENOTSUPPORTED;
        }
        
        status = FdtFindResources(tree, FdtReadBe32(line), &gpio);
        if (status != OS_EOK) {
            return status;
        }
        
        status = FdtScalar(&gpio.View, "#gpio-cells", &cells);
        if (status != OS_EOK || cells != 2 ||
            FdtProperty(&gpio.View, "gpio-controller", &legacyLength) == NULL) {
            return OS_ENOTSUPPORTED;
        }
        // Save the validated controller's node offset for setup. GpioPresent
        // distinguishes an absent reset line from GPIO pin zero.
        reset->GpioPresent = 1;
        reset->Controller = gpio.NodeOffset;
        reset->Pin = FdtReadBe32(line + 4);
        reset->Flags = FdtReadBe32(line + 8);
        // Only the polarity flag is supported. Reject settings such as
        // open-drain, since they change how the reset line must be driven.
        if (reset->Flags & ~1U) {
            return OS_ENOTSUPPORTED;
        }
    }

    // Return all timings in microseconds so the reset code can use them
    // without checking which firmware binding supplied them.
    status = __EthernetOptionalScalar(phy, "reset-assert-us", &reset->AssertUs);
    if (status != OS_EOK) {
        return status;
    }

    status = __EthernetOptionalScalar(phy, "reset-deassert-us", &reset->DeassertUs);
    if (status != OS_EOK) {
        return status;
    }

    if (legacy != NULL) {
        // The legacy reset line requires timings from the MAC binding only.
        if (reset->AssertUs || reset->DeassertUs) {
            return OS_ENOTSUPPORTED;
        }
        
        // The MAC binding uses milliseconds and defaults to a 1 ms pulse.
        // Check before multiplying so the exported duration cannot wrap.
        duration = 1;
        status = __EthernetOptionalScalar(mac, "phy-reset-duration", &duration);
        if (status != OS_EOK || duration > UINT32_MAX / 1000) {
            return OS_EINVALPARAMS;
        }

        reset->AssertUs = duration * 1000;
        duration = 0;
        
        status = __EthernetOptionalScalar(mac, "phy-reset-post-delay", &duration);
        if (status != OS_EOK || duration > UINT32_MAX / 1000) {
            return OS_EINVALPARAMS;
        }

        reset->DeassertUs = duration * 1000;
    }
    return OS_EOK;
}

static oserr_t
__EthernetMdio(
    _In_    const struct FdtTree*               tree,
    _In_    const struct FdtResources*          mac,
    _In_    const struct FdtResources*          phy,
    _InOut_ struct DeviceEthernetConfiguration* config)
{
    const struct FdtResources* mdio;
    const struct FdtResources* parent;
    uint64_t                   base;
    uint64_t                   size;
    uint64_t                   macBase;
    uint64_t                   macSize;
    oserr_t                    status;

    // The PHY's reg property must contain one MDIO address from 0 to 31.
    // Clause 45 PHYs require a different management protocol and are unsupported.
    mdio = FdtTreeParent(tree, phy->NodeOffset);
    if (mdio == NULL || mdio->AddressCells != 1 || mdio->SizeCells != 0) {
        return OS_EINVALPARAMS;
    }

    if (phy->RegLength != 4 || FdtReadBe32(phy->Reg) > 31) {
        return OS_EINVALPARAMS;
    }

    if (FdtCompatible(&phy->View, "ethernet-phy-ieee802.3-c45")) {
        return OS_ENOTSUPPORTED;
    }

    config->PhyAddress = FdtReadBe32(phy->Reg);
    config->PhyNode = phy->NodeOffset;
    config->MdioNode = mdio->NodeOffset;
    if (mdio->NodeOffset == mac->NodeOffset) {
        // PHYs directly below the MAC use its integrated MDIO registers.
        // The zero-initialized offset and length describe that layout.
        return OS_EOK;
    }

    // A separate MDIO node must be a GENET child. An external controller
    // would need a connection to its driver.
    parent = FdtTreeParent(tree, mdio->NodeOffset);
    if (parent != mac || !FdtCompatible(&mdio->View, "brcm,genet-mdio-v5")) {
        return OS_ENOTSUPPORTED;
    }

    status = FdtRawRegister(mdio, 0, &base, &size);
    if (status != OS_EOK) {
        return status;
    }

    status = FdtRawRegister(mac, 0, &macBase, &macSize);
    if (status != OS_EOK) {
        return status;
    }

    // The child's reg is an offset within the MAC's first register range.
    // Check that the full interval fits before converting it to 32 bits.
    if (!size || base > UINT32_MAX || size > UINT32_MAX ||
        !FdtContainsRange(0, macSize, base, size)) {
        return OS_EINVALPARAMS;
    }

    config->MdioOffset = (uint32_t)base;
    config->MdioLength = (uint32_t)size;
    return OS_EOK;
}

oserr_t
FdtEthernetConfiguration(
    _In_  const struct FdtTree*                tree,
    _In_  uint32_t                             node,
    _Out_ struct DeviceEthernetConfiguration* configuration)
{
    const struct FdtResources*         mac = FdtTreeNode(tree, node);
    struct FdtResources                phy;
    struct __EthernetChildren          children = { .MacNode = node };
    struct DeviceEthernetConfiguration config = { .Version = 1 };
    uint32_t                           phandle;
    uint32_t                           speed = 0;
    uint32_t                           length;
    oserr_t                            status;

    if (mac == NULL || mac->Disabled || mac->ParentDisabled) {
        return OS_ENOENT;
    }

    if (mac->Malformed || mac->ParentMalformed) {
        return OS_EINVALPARAMS;
    }

    if (!FdtCompatible(&mac->View, "brcm,bcm2711-genet-v5") &&
        !FdtCompatible(&mac->View, "raspberrypi,rp1-gem")) {
        return OS_ENOTSUPPORTED;
    }

    // This path requires a PHY reached through MDIO. Fixed links and managed
    // in-band links need different setup. Check both fixed-link binding forms:
    // an older property on the MAC and a newer child node.
    if (FdtProperty(&mac->View, "fixed-link", &length) != NULL ||
        FdtProperty(&mac->View, "managed", &length) != NULL) {
        return OS_ENOTSUPPORTED;
    }

    status = FdtVisitSubtree(tree, node, __EthernetInspectChild, &children);
    if (status != OS_EOK) {
        return status;
    }

    if (children.FixedLink) {
        return OS_ENOTSUPPORTED;
    }

    // Resolve the referenced PHY before copying optional settings. Its parent
    // determines whether MDIO is integrated or uses a separate register block.
    status = FdtScalar(&mac->View, "phy-handle", &phandle);
    if (status != OS_EOK) {
        return status;
    }

    status = FdtFindResources(tree, phandle, &phy);
    if (status != OS_EOK) {
        return status;
    }

    status = __EthernetMdio(tree, mac, &phy, &config);
    if (status != OS_EOK) {
        return status;
    }

    status = __EthernetMode(&mac->View, &config.Mode);
    if (status != OS_EOK) {
        return status;
    }

    status = __EthernetMac(&mac->View, &config);
    if (status != OS_EOK) {
        return status;
    }

    status = __EthernetSpeed(&mac->View, &config.MaxSpeed);
    if (status != OS_EOK) {
        return status;
    }

    status = __EthernetSpeed(&phy.View, &speed);
    if (status != OS_EOK) {
        return status;
    }

    // Use the lower nonzero speed limit from the MAC and PHY bindings.
    // Link negotiation must respect both limits.
    if (speed && (!config.MaxSpeed || speed < config.MaxSpeed)) {
        config.MaxSpeed = speed;
    }

    status = __EthernetReset(tree, &mac->View, &phy.View, &config.Reset);
    if (status != OS_EOK) {
        return status;
    }

    // All fields must pass validation before updating the caller's configuration.
    // On failure, leave the previous configuration unchanged.
    *configuration = config;
    return OS_EOK;
}

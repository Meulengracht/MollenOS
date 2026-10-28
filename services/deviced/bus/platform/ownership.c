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

#include "ownership.h"

// Standard PCI hosts belong to PCI discovery, including hosts without device_type.
static const char* const g_pciHostCompatibles[] = {
    "pci-host-ecam-generic",
    "pci-host-cam-generic",
    NULL
};

// The ARM kernel owns interrupt controllers, the architectural timer and PSCI.
static const char* const g_armKernelCompatibles[] = {
    "arm,gic-400",
    "arm,cortex-a15-gic",
    "arm,gic-v3",
    "arm,armv8-timer",
    "arm,psci-0.2",
    "arm,psci-1.0",
    NULL
};

// Broadcom rules cover Pi 4/5 bindings and apply wherever these devices occur.
static const char* const g_broadcomCompatibles[] = {
    // PCI discovery owns these host controllers.
    "brcm,bcm2711-pcie",
    "brcm,bcm2712-pcie",
    // The kernel owns these interrupt controllers.
    "brcm,bcm2836-l1-intc",
    "brcm,bcm2712-mip",
    // PCI host drivers currently program these reset controllers directly.
    "brcm,bcm7216-pcie-sata-rescal",
    "brcm,brcmstb-reset",
    NULL
};

static int
__MatchesCompatible(
    _In_ const struct FdtResources* node,
    _In_ const char* const*         compatibles)
{
    unsigned int i;

    for (i = 0; compatibles[i] != NULL; i++) {
        if (FdtCompatible(&node->View, compatibles[i])) {
            return 1;
        }
    }
    return 0;
}

int
PlatformNodeIsManaged(
    _In_ const struct FdtTree*      tree,
    _In_ const struct FdtResources* node)
{
    // Memory and MSI controllers are kernel-owned; PCI has its own enumerator.
    if (node->IsMemory || node->IsMsiController || node->IsPci) {
        return 1;
    }

    // Skip the resolved console node so platform discovery does not publish it again.
    if (FdtTreeConsole(tree) == FdtTreeNode(tree, node->NodeOffset)) {
        return 1;
    }

    return __MatchesCompatible(node, g_pciHostCompatibles) ||
        __MatchesCompatible(node, g_armKernelCompatibles) ||
        __MatchesCompatible(node, g_broadcomCompatibles);
}

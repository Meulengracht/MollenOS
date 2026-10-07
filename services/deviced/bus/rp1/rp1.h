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
 * RP1 is a controller chip used on some Raspberry Pi boards. It connects to the
 * main system over PCI Express (PCIe), a link for connecting devices. The system
 * lists RP1 as one PCI device, while firmware describes its internal hardware
 * blocks in a device tree: a list of devices and resources supplied at startup.
 * 
 */

#ifndef __DEVICED_RP1_H__
#define __DEVICED_RP1_H__

#include <firmware/rp1.h>

#define RP1_VENDOR_ID        0x1de4
#define RP1_DEVICE_ID        0x0001

// Define the expected RP1 revision
#define RP1_REVISION_C0      2

// RP1's peripheral-control registers occupy a 0x1000-byte window at this offset
// inside BAR1, the second address range assigned to RP1 by PCI.
#define RP1_PCIE_APBS_OFFSET 0x108000U
#define RP1_PCIE_APBS_LENGTH 0x1000U

// Forward declarations
struct PciDevice;
struct DmPublicationGroup;

/**
 * @brief One hardware block inside RP1, plus the state deviced needs to list it.
 * The firmware description contains names and other data that point into the
 * device tree. The PCI host keeps that data mapped while this child exists.
 */
struct Rp1Child {
    struct Rp1Child*    Next;
    uuid_t              DeviceId;
    struct FdtRp1Device Firmware;
};

/**
 * @brief Inventory and publication state for the hardware blocks inside one RP1
 * PCI device. Creating this object reads their descriptions but does not add
 * device-manager entries or set up DMA and interrupts. The PCI address ranges
 * are copied into this object. The firmware view is borrowed, so the PCI host
 * must keep its firmware data mapped while this object exists.
 */
struct Rp1Bus {
    // We should not clean this up, it's not managed by us.
    const struct FdtPciHost* Host;

    struct Rp1Child* Children;
    unsigned int     ChildCount;

    // A copy of the pci address range used to check
    // against child resources
    struct PciBar    Bars[6];
};

/**
 * @brief Adds device-manager entries for RP1's firmware-described hardware blocks.
 * Each entry is placed below the RP1 PCI device. This does not allow drivers to
 * match yet; PCI enables matching only after the full device tree is listed.
 * If adding an entry fails, the group remembers entries already added so PCI
 * can remove them. The group clears each saved child ID after successful removal.
 *
 * @param bus Inventory whose children will be added.
 * @param endpoint Published PCI device that owns the RP1 hardware blocks.
 * @param group Host group that will handle driver matching and removal.
 * @return OS_EOK if every child is added, or the first registration error.
 */
__EXTERN oserr_t
Rp1BusPublish(
    _InOut_ struct Rp1Bus*             bus,
    _In_    const struct PciDevice*    endpoint,
    _InOut_ struct DmPublicationGroup* group);

/**
 * @brief Builds an in-memory list of the hardware blocks firmware places inside
 * this RP1 device. It checks their addresses against the PCI memory ranges and
 * does not publish them or set up DMA or interrupts. The caller must keep the
 * host's firmware data available until Rp1BusDestroy finishes.
 *
 * @param host Firmware description for the PCI host containing RP1.
 * @param bars Six address ranges reported for the RP1 PCI device. BAR1 must be
 *             assigned and large enough to contain RP1's control registers.
 * @param busOut Receives the new inventory on success; set to NULL on failure.
 * @return OS_EOK on success, or an error if the inputs, PCI ranges, firmware data,
 *         or memory allocation are invalid. Partial inventory is freed on error.
 */
__EXTERN oserr_t
Rp1BusCreate(
    _In_  const struct FdtPciHost* host,
    _In_  const struct PciBar*     bars,
    _Out_ struct Rp1Bus**          busOut);

/**
 * @brief Frees the inventory after the host publication group has been removed.
 * If a child still has a registered ID, keep the inventory for a later retry.
 * The group still needs that saved ID location to finish removing its entries.
 *
 * @param bus Inventory to release. NULL is allowed.
 */
__EXTERN void
Rp1BusDestroy(
    _In_ struct Rp1Bus* bus);

#endif

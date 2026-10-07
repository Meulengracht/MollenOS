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

/**
 * @brief One hardware block inside RP1, plus the state deviced needs to list it.
 * The firmware description contains names and other data that point into the
 * device tree. The PCI host keeps that data mapped while this child exists.
 */
struct Rp1Child {
    struct Rp1Child*    Next;
    uuid_t              DeviceId;
    int                 BindingEnabled;
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
    // ID of the parent PCI device
    uuid_t                   DeviceId;

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
 * If adding an entry fails, entries already added remain recorded so PCI can
 * remove them. Repeating this call skips children that already have an ID.
 *
 * @param bus Inventory whose children will be added.
 * @param endpoint Published PCI device that owns the RP1 hardware blocks.
 * @return OS_EOK if every child is added, or an error if the parent ID is invalid
 *         or a child cannot be added. On failure, call Rp1BusUnpublish to remove
 *         any children that were added before the error.
 */
__EXTERN oserr_t
Rp1BusPublish(
    _InOut_ struct Rp1Bus*          bus,
    _In_    const struct PciDevice* endpoint);

/**
 * @brief Removes the device-manager entries created for this RP1 inventory.
 * The parent PCI entry is owned by PCI and is left in place. Stop programs using
 * these devices and remove any devices registered beneath them before calling.
 * If removal fails, IDs for entries not yet removed remain available for retry.
 *
 * @param bus Inventory whose published children will be removed.
 * @return OS_EOK if all children are removed, or the first removal error. Entries
 *         removed before an error stay removed; call again to retry the rest.
 */
__EXTERN oserr_t
Rp1BusUnpublish(
    _InOut_ struct Rp1Bus* bus);

/**
 * @brief Allows the system to look for drivers for each RP1 child.
 * Call only after all PCI and RP1 device entries have been added. If enabling
 * one child fails, earlier children remain enabled and a later call skips them.
 *
 * @param bus Inventory whose children should be made available to drivers.
 * @return OS_EOK if every child is enabled, or the error for the first child
 *         that could not be enabled. Call again to retry.
 */
__EXTERN oserr_t
Rp1BusEnableBinding(
    _InOut_ struct Rp1Bus* bus);

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
 * @brief Removes published children and frees the inventory after clients stop.
 * Child entries must be removed before their descriptions can be freed. If an
 * entry cannot be removed, this function keeps the inventory so cleanup can be
 * retried; the caller must keep its firmware data and PCI resources available.
 *
 * @param bus Inventory to release. NULL is allowed.
 */
__EXTERN void
Rp1BusDestroy(
    _In_ struct Rp1Bus* bus);

#endif

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

// Hardware revision supported by this driver.
#define RP1_REVISION_C0      2

// RP1's peripheral-control registers (APBS) occupy 0x1000 bytes at this offset
// inside BAR1. A PCI Base Address Register (BAR) describes a device address
// range; BAR1 is the second slot, counted from zero.
#define RP1_PCIE_APBS_OFFSET 0x108000U
#define RP1_PCIE_APBS_LENGTH 0x1000U

// Forward declarations
struct PciDevice;
struct DmPublicationGroup;
struct PciFunctionHandler;

// Identifies the PCI attachment owned by RP1 when serving child resource queries.
extern const struct PciFunctionHandler g_rp1PciHandler;

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
 * @brief List of hardware blocks inside one RP1 PCI device and their saved IDs.
 * Creating this object reads their descriptions but does not add device-manager
 * entries or set up interrupts or direct memory access (DMA), which lets devices
 * access memory themselves. PCI address ranges are copied here. Host and the
 * firmware strings point to existing data, which the PCI host must keep
 * available and mapped while this object exists.
 */
struct Rp1Bus {
    // The PCI host owns this description; do not free it when releasing the bus.
    const struct FdtPciHost* Host;

    struct Rp1Child* Children;
    unsigned int     ChildCount;

    // Copies of the six PCI address ranges used to check that each child's
    // registers fit within memory assigned to RP1.
    struct PciBar    Bars[6];
};

/**
 * @brief Adds device-manager entries for RP1's firmware-described hardware blocks.
 * Each entry is placed below the RP1 PCI device. This does not ask the device
 * manager to find drivers yet; the PCI code requests that after adding all
 * device entries. RP1 driver startup remains blocked until DMA and interrupts
 * are ready.
 * If adding an entry fails, the group remembers entries already added so PCI
 * can remove them. The group clears each saved child ID after successful removal.
 *
 * @param bus List of RP1 hardware blocks to add.
 * @param endpoint RP1 PCI device, already registered with the device manager.
 * @param group Tracks added entries so drivers can be found or entries removed
 *              together, and saves where each child's device ID is stored.
 * @return OS_EOK if every child is added, or the first registration error.
 */
__EXTERN oserr_t
Rp1BusPublish(
    _InOut_ struct Rp1Bus*             bus,
    _In_    const struct PciDevice*    endpoint,
    _InOut_ struct DmPublicationGroup* group);

/**
 * @brief Builds an in-memory list of the hardware blocks firmware places inside
 * this RP1 device. It checks their addresses against the PCI memory ranges.
 * Device-manager registration, direct memory access (DMA), and interrupt setup
 * happen separately. Keep the host description and its firmware data available
 * until Rp1BusDestroy has freed the list.
 *
 * @param host Firmware description for the PCI host containing RP1.
 * @param bars Six address ranges reported for the RP1 PCI device. BAR1 must be
 *             assigned and large enough to contain RP1's control registers.
 * @param busOut Receives the new list on success. Set to NULL on failure after
 *               the initial check that host, bars, and busOut are non-NULL.
 * @return OS_EOK on success, or an error for invalid inputs, unusable PCI ranges
 *         or firmware data, or failed memory allocation. Any partly built list
 *         is freed on error.
 */
__EXTERN oserr_t
Rp1BusCreate(
    _In_  const struct FdtPciHost* host,
    _In_  const struct PciBar*     bars,
    _Out_ struct Rp1Bus**          busOut);

/**
 * @brief Free the list after all of its device-manager entries have been removed.
 * If a child still has a registered ID, this function leaves the entire list
 * allocated. The registration group still needs the stored IDs to remove those
 * entries. Call this function again after their removal succeeds.
 *
 * @param bus List to release. NULL is allowed.
 */
__EXTERN void
Rp1BusDestroy(
    _In_ struct Rp1Bus* bus);

#endif

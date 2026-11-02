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

#include <ddk/resources.h>
#include <os/types/device.h>
#include <os/usched/mutex.h>
#include <stdatomic.h>

#include <firmware/rp1.h>
#include <bus/pci/bars.h>

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
struct Rp1Runtime;

// Identifies the PCI attachment owned by RP1 when serving child resource queries.
extern const struct PciFunctionHandler g_rp1PciHandler;

/**
 * @brief Registered memory resources and activation state for one RP1 child.
 * Published records resources already created, including partial setup after an
 * error. Prepared records whether the child has completed DevicePrepare.
 */
struct Rp1ChildResources {
    struct DeviceResources Published;
    int                    Prepared;
};

/**
 * @brief One hardware block inside RP1 and its device-manager state.
 * Firmware contains names and other data that point into the device tree.
 * Bus owns this child. Each resource reference keeps the bus and its PCI parent
 * alive, and the parent keeps firmware mapped. Without a resource reference,
 * the caller must keep the bus and host alive while accessing the child.
 */
struct Rp1Child {
    struct Rp1Child*         Next;
    struct Rp1Bus*           Bus;
    uuid_t                   DeviceId;
    struct FdtRp1Device      Firmware;
    struct Rp1ChildResources Resources;
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
    // Protects Ready, Runtime and every child's Resources. When both are needed,
    // take the PCI lock before this lock. Child requests take only this lock
    // and must not enter PCI or registry locking paths while holding it. PCI
    // access may take the host's configuration lock last during setup/teardown.
    struct usched_mtx Lock;

    // The PCI host owns this description; do not free it when releasing the bus.
    const struct FdtPciHost* Host;
    // Set by PCI attachment. Child resource callbacks take their own parent references.
    struct PciDevice*        Parent;
    // Each child resource reference also keeps Parent alive. Atomic because registry
    // callbacks for different children may run concurrently.
    atomic_uint              HostBusReferences;

    struct Rp1Child* Children;
    unsigned int     ChildCount;

    // Copies of the six PCI address ranges used to check that each child's
    // registers fit within memory assigned to RP1.
    struct PciBar      Bars[6];
    // Owns messages and mappings, including unfinished startup/shutdown.
    struct Rp1Runtime* Runtime;
    int                Ready;
};

/**
 * @brief Adds device-manager entries for RP1's firmware-described hardware blocks.
 * Each entry is placed below the RP1 PCI device. This does not ask the device
 * manager to find drivers or prepare hardware yet. PCI prepares DMA and interrupts
 * separately after adding all device entries, then enables driver binding.
 * Preparation failure leaves these descriptions registered for retry.
 * If adding an entry fails, the group remembers entries already added so PCI
 * can remove them. The group clears each saved child ID after successful removal.
 *
 * @param bus List of RP1 hardware blocks to add.
 * @param endpoint RP1 PCI device, already registered with the device manager.
 *                 Must be the parent recorded when this bus was attached.
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
 * Published IDs and outstanding resource references both keep the list alive.
 * The group still needs the stored IDs while removing entries, even after the
 * registry has released its resource reference. Release callbacks never free it.
 *
 * @param bus List to release. NULL is allowed.
 *            The owner must exclude publication, removal of registry entries,
 *            and new resource references while calling this function.
 *            Hold the PCI lock if a runtime exists; this function takes bus->Lock.
 * @return OS_EOK after freeing the list (or for NULL), OS_EBUSY without freeing
 *         anything while IDs or resource references remain. The caller must
 *         preserve the parent and retry, rather than continue enclosing cleanup.
 */
__EXTERN oserr_t
Rp1BusDestroy(
    _In_ struct Rp1Bus* bus);

/**
 * @brief Supply register resources or handle a child's activation request.
 * 
 * @param child Child kept alive by a resource reference. Caller holds Bus->Lock.
 * @param request Resource query or activation request.
 * @param buffer Request/result storage.
 * @param length Size of the request buffer.
 * @return OS_EOK on success or the request error. Resources created before an
 *         error remain recorded in the child for retry or destruction.
 */
__EXTERN oserr_t
Rp1ChildResourceControl(
    _InOut_ struct Rp1Child*     child,
    _In_    enum OSIOCtlRequest request,
    _InOut_ void*               buffer,
    _In_    size_t              length);

/**
 * @brief Release registered resources after all child users have stopped.
 * 
 * @param child Unpublished child whose hardware has stopped.
 * @return Cleanup status; failure leaves the remaining resources owned for retry.
 */
__EXTERN oserr_t
Rp1ChildResourcesDestroy(
    _InOut_ struct Rp1Child* child);

#endif

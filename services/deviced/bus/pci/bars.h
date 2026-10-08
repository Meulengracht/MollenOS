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

#ifndef __PCI_BARS_INTERFACE__
#define __PCI_BARS_INTERFACE__

#include <os/osdefs.h>

// Forward declarations
typedef struct PciHost PciHost_t;
struct BusDevice;
struct FdtPciHost;

enum PciBarState {
    PciBarAbsent,
    PciBarUnassigned,
    PciBarAssigned,
    PciBarOutsideWindow,
    PciBarInvalid
};

// One PCI address range and whether the host can make it available.
struct PciBar {
    uint64_t         BusAddress; // Address the PCI device uses to access this resource.
    uint64_t         CpuAddress; // Host CPU address corresponding to BusAddress, if translatable.
    uint64_t         Size;       // Size of the resource in bytes.
    uint32_t         Space;      // Resource type: PCI I/O (1), 32-bit memory (2), or 64-bit memory (3).
    uint32_t         Attributes; // PCI BAR flags and other attributes describing the resource.
    enum PciBarState State;      // Whether the BAR is absent, unassigned, usable, outside the host window, or invalid.
};

// Resources supplied to a function handler, with borrowed firmware data.
struct PciFunctionResources {
    struct PciBar            Bars[6];
    const struct FdtPciHost* Firmware;
};


/**
 * @brief Determine the size, type, and current assignment of a device's BARs.
 *
 * Temporarily disables memory and I/O decoding while it probes BAR masks, then
 * restores the device's original BAR values and command setting. The resulting
 * descriptions are written to all six entries in @p bars; header types with
 * fewer BARs leave the unused entries absent. This function only describes the
 * resources: it does not assign addresses or create device I/O resources.
 *
 * @param host PCI host that provides configuration access and address translation.
 * @param device PCI device whose BARs are being examined.
 * @param headerType PCI header type, used to determine how many BARs to probe.
 * @param bars Output array receiving the six BAR descriptions.
 */
__EXTERN void
PciProbeBars(
    _In_ struct PciHost*         host,
    _In_ const struct BusDevice* device,
    _In_ uint32_t                headerType,
    _In_ struct PciBar           bars[6]);

/**
 * @brief Turn an existing address range into a BAR description.
 *
 * Copies the supplied type, attributes, bus address, and size into @p bar, then
 * asks the host to translate the bus address for CPU access when translation is
 * available. The state records whether the range is absent, unassigned,
 * successfully assigned, outside the host's address windows, or invalid due to
 * an overflowing extent. The original bus address is retained even if it
 * cannot be translated; this function does not change the device's BAR.
 *
 * @param host PCI host used to translate the address, if it supports translation.
 * @param space Resource type: PCI I/O, 32-bit memory, or 64-bit memory.
 * @param attributes PCI BAR flags associated with the range.
 * @param address Existing address assigned to the resource on the PCI bus.
 * @param size Length of the resource in bytes.
 * @param bar Output structure receiving the description and usability state.
 */
__EXTERN void
PciDescribeBar(
    _In_ struct PciHost* host,
    _In_ uint32_t        space,
    _In_ uint32_t        attributes,
    _In_ uint64_t        address,
    _In_ uint64_t        size,
    _In_ struct PciBar*  bar);

/**
 * @brief Log warnings for BAR descriptions that cannot currently be used.
 *
 * Checks all six descriptions and warns when a BAR is unassigned, outside the
 * host's address windows, or invalid. Absent and successfully assigned BARs do
 * not produce warnings. This function only reports problems; it does not modify
 * the descriptions, assign BAR addresses, or register device resources.
 *
 * @param host PCI host used to identify the segment in diagnostic messages.
 * @param device PCI device used to identify the function in diagnostic messages.
 * @param bars Six BAR descriptions to check, typically produced by
 *             PciProbeBars.
 */
__EXTERN void
PciDiagnoseBars(
    _In_ struct PciHost*         host,
    _In_ const struct BusDevice* device,
    _In_ const struct PciBar     bars[6]);

/**
 * @brief Create device I/O resource entries for usable BAR descriptions.
 *
 * Checks all six descriptions and creates an entry in the matching device I/O
 * slot only for assigned BARs whose address range fits the platform's address
 * limits. Memory BARs become memory resources. PCI I/O BARs are represented as
 * port I/O resources when the host policy supports ports; otherwise they are
 * represented as memory resources. This prepares resources for device use but
 * does not assign or rewrite BAR addresses in PCI configuration space.
 *
 * @param host PCI host whose I/O resource policy determines how BARs are exposed.
 * @param device Device whose I/O resource entries are populated.
 * @param bars Six BAR descriptions, typically produced by PciProbeBars.
 */
__EXTERN void
PciRegisterBars(
    _In_ struct PciHost*     host,
    _In_ struct BusDevice*   device,
    _In_ const struct PciBar bars[6]);

/**
 * @brief Reads and publishes a function's BAR resources.
 */
__EXTERN void
PciReadBars(
    _In_ PciHost_t*        bus,
    _In_ struct BusDevice* device,
    _In_ uint32_t          headerType);

#endif //!__PCI_BARS_INTERFACE__

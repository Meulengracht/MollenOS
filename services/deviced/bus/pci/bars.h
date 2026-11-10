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
struct PciDevice;
struct FdtPciHost;

enum PciBarState {
    PciBarAbsent,        // No resource size was reported.
    PciBarUnassigned,    // A resource exists, but its bus address is zero.
    PciBarAssigned,      // The bus range has a valid CPU address.
    PciBarOutsideWindow, // The host could not translate the bus range.
    PciBarInvalid        // The BAR encoding or address range is invalid.
};

/**
 * @brief A PCI address range, its CPU address and whether it can be used.
 */
struct PciBar {
    uint64_t         BusAddress; // PCI bus address decoded by this BAR.
    uint64_t         CpuAddress; // CPU address for BusAddress, if translation succeeds.
    uint64_t         Size;       // Size of the resource in bytes.
    uint32_t         Space;      // Resource type: PCI I/O (1), 32-bit memory (2), or 64-bit memory (3).
    uint32_t         Attributes; // PCI BAR flags and other attributes describing the resource.
    enum PciBarState State;      // Whether this range can be used, or why it cannot.
};

/**
 * @brief BAR descriptions supplied to a function handler and the host's firmware data.
 */
struct PciFunctionResources {
    struct PciBar            Bars[6];
    const struct FdtPciHost* Firmware;
};

/**
 * @brief Reads the size, type and current address of each BAR.
 *
 * Call this for a device or bridge header before registering its I/O resources.
 * Probing disables memory and I/O decoding while reading the size masks, then
 * restores the BAR values and command register. All six entries in bars are
 * filled in. Unused entries are marked absent.
 *
 * @param host PCI host that provides configuration access and address translation.
 * @param device PCI device whose BARs are being examined.
 * @param headerType PCI header type, used to determine how many BARs to probe.
 * @param bars Output array receiving the six BAR descriptions.
 * @return None.
 */
__EXTERN void
PciProbeBars(
    _In_  struct PciHost*         host,
    _In_  const struct BusDevice* device,
    _In_  uint32_t                headerType,
    _Out_ struct PciBar           bars[6]);

/**
 * @brief Describes a BAR's bus range and translates it to a CPU address.
 *
 * If firmware described the host windows, the range must fit one of them.
 * Otherwise, the bus address is also used as the CPU address. State records
 * whether the BAR can be used, including absent, unassigned and invalid ranges.
 * The description keeps the original bus address even when translation fails.
 * The device's BAR is unchanged.
 *
 * @param host PCI host whose windows are used for translation.
 * @param space Resource type: PCI I/O, 32-bit memory, or 64-bit memory.
 * @param attributes PCI BAR flags associated with the range.
 * @param address Existing address assigned to the resource on the PCI bus.
 * @param size Length of the resource in bytes.
 * @param bar Receives the range, CPU address and state.
 * @return None.
 */
__EXTERN void
PciDescribeBar(
    _In_  struct PciHost* host,
    _In_  uint32_t        space,
    _In_  uint32_t        attributes,
    _In_  uint64_t        address,
    _In_  uint64_t        size,
    _Out_ struct PciBar*  bar);

/**
 * @brief Logs warnings for BARs that cannot be used.
 *
 * Checks all six entries. Unassigned, invalid and out-of-window BARs produce
 * warnings. Absent and assigned BARs are skipped. The descriptions and device
 * resources are left unchanged.
 *
 * @param host PCI host used to identify the segment in diagnostic messages.
 * @param device PCI device used to identify the function in diagnostic messages.
 * @param bars The six BAR descriptions to check.
 * @return None.
 */
__EXTERN void
PciDiagnoseBars(
    _In_ struct PciHost*         host,
    _In_ const struct BusDevice* device,
    _In_ const struct PciBar     bars[6]);

/**
 * @brief Creates device I/O resources for assigned BARs.
 *
 * Each range must fit the platform's address limits. Memory BARs become memory
 * resources. I/O BARs become port or memory resources according to the host's
 * I/O policy. Entries use the same slots as the BAR descriptions.
 * BAR addresses in configuration space stay unchanged.
 *
 * If a range is unsupported or resource creation fails, log the error and
 * continue with the remaining BARs.
 *
 * @param host PCI host whose I/O resource policy determines how BARs are exposed.
 * @param device Device whose I/O resource entries are populated.
 * @param bars The six BAR descriptions to register.
 * @return None.
 */
__EXTERN void
PciRegisterBars(
    _In_ struct PciHost*     host,
    _In_ struct BusDevice*   device,
    _In_ const struct PciBar bars[6]);

/**
 * @brief Probe a function's BARs, report unusable ranges, and register I/O resources.
 *
 * Call this for a device or bridge header. BAR probing temporarily disables
 * memory and I/O decoding, then restores the original configuration.
 *
 * @param bus PCI host used for configuration access and resource registration.
 * @param device Device whose I/O resource entries are populated.
 * @param headerType PCI header type selecting the number of BARs to probe.
 * @return None.
 */
__EXTERN void
PciReadBars(
    _In_ PciHost_t*        bus,
    _In_ struct BusDevice* device,
    _In_ uint32_t          headerType);

/**
 * @brief Assigns addresses to BARs that have not been assigned yet. This must be
 * called after the host has been fully scanned, and before anything on it is
 * published.
 *
 * Addresses are taken from the firmware windows of the host and from the ranges
 * that bridges already forward. Ranges used by already assigned BARs and by
 * sibling bridge windows are left out, so new addresses never overlap them. If
 * an assignment fails, the ones that succeeded before it stay in the inventory,
 * which makes it safe for startup to retry. Hosts with no firmware description
 * keep their existing resource policy. If a failed write cannot be rolled back,
 * later calls return OS_ENOTSUPPORTED without touching configuration space
 * until the host is torn down.
 *
 * @param root The root device of the host. The device tree below it must be fully
 *             scanned and stable, and no drivers can be bound yet.
 * @return OS_EOK on success, OS_ENOTSUPPORTED after a failed rollback, or an
 *         error from allocation, address translation or writing to configuration
 *         space.
 */
__EXTERN oserr_t
PciAssignHostBars(
    _In_ struct PciDevice* root);

#endif //!__PCI_BARS_INTERFACE__

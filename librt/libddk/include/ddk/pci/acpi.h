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

#ifndef __DDK_PCI_ACPI_H__
#define __DDK_PCI_ACPI_H__

#include <ddk/pci/windows.h>
#include <os/types/dma.h>

// A segment has 256 buses, so a valid path can never go through more than
// 255 bridges.
#define ACPI_PCI_MAX_BRIDGES 255

enum AcpiPciQueryType {
    AcpiPciQueryRoot,
    AcpiPciQueryWindow,
    AcpiPciQueryInterrupt
};

// Set in AcpiPciDma.Flags when devices can address memory directly (the address
// a device uses is the physical address). DMA is only allowed when this flag is
// set and DmaStatus is OS_EOK. An older kernel leaves these fields zeroed, which
// means DMA is not allowed.
#define ACPI_PCI_DMA_DIRECT 0x00000001U

/**
 * @brief The DMA rules that apply to every device below one PCI root.
 *
 * AddressLimit is the highest address a device can reach. Addresses start at
 * zero. CachePolicy holds an SHMDeviceCachePolicy value. Flags can only hold
 * ACPI_PCI_DMA_DIRECT for now. Reading these rules does not allow a device to
 * start DMA, as bus mastering is left switched off.
 */
typedef struct AcpiPciDma {
    uint64_t AddressLimit;
    uint32_t CachePolicy;
    uint32_t Flags;
} AcpiPciDma_t;

/**
 * @brief Describes one PCI root: its id, bus range, number of CPU windows and
 * DMA rules.
 *
 * DmaStatus holds an oserr_t value. It is kept apart from the result of the
 * root query, so a root with broken DMA information can still be used for
 * configuration and interrupts. Only use Dma when DmaStatus is OS_EOK and
 * ACPI_PCI_DMA_DIRECT is set. The first four fields keep their old layout, so
 * older code reading them is not affected.
 */
struct AcpiPciRoot {
    uint32_t     Id;
    uint32_t     WindowCount;
    uint16_t     Segment;
    uint8_t      BusStart;
    uint8_t      BusEnd;
    uint32_t     DmaStatus;
    AcpiPciDma_t Dma;
};

struct AcpiPciAddress {
    uint8_t Device;
    uint8_t Function;
};

/**
 * @brief Describes a query for AcpiQueryPci. The fields used depend on Type.
 *
 * AcpiPciQueryRoot: Index selects the root, starting from 0. If the DMA
 *   information for a root could not be read, the error is stored in
 *   Root.DmaStatus and the query itself still succeeds.
 * AcpiPciQueryWindow: RootId selects the root, and Index selects the window.
 * AcpiPciQueryInterrupt: RootId selects the root, and Bridges holds the path of
 *   bridges from the root down to the bus of the device. This is how we find the
 *   bus in the ACPI namespace. The _PRT of that bus is then searched for an
 *   entry matching Address and Pin. Pin must be 1 to 4. Only that one bus is
 *   searched, so if there is no match, it is up to the caller to swizzle the
 *   pin and query again for the parent bridge.
 */
struct AcpiPciQuery {
    uint32_t              Type;
    uint32_t              RootId;
    uint32_t              Index;
    uint16_t              BridgeCount;
    uint8_t               Pin;
    uint8_t               Reserved;
    struct AcpiPciAddress Address;
    struct AcpiPciAddress Bridges[ACPI_PCI_MAX_BRIDGES];
};

struct AcpiPciInterrupt {
    int32_t  Line;
    uint32_t Conform;
};

union AcpiPciResult {
    struct AcpiPciRoot      Root;
    struct PciAddressWindow Window;
    struct AcpiPciInterrupt Interrupt;
};

/**
 * @brief Looks up PCI root and window information that the kernel copied from
 * ACPI, or evaluates _PRT to find the INTx route of a device.
 *
 * @param query The query to run. Fields that are not used by the query type
 *              must be zero.
 * @param result Set to the result of the query on success.
 * @return OS_EOK on success, or OS_ENOENT when there are no more entries to
 *         enumerate or the device has no route. On failure result is not changed.
 */
DDKDECL(oserr_t,
AcpiQueryPci(
    _In_  const struct AcpiPciQuery* query,
    _Out_ union AcpiPciResult*      result));

#endif //!__DDK_PCI_ACPI_H__

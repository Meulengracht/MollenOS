/**
 * MollenOS
 *
 * Copyright 2015, Philip Meulengracht
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
 *
 * MollenOS PCI Bus Driver
 * - Enumerates the bus and registers the devices/controllers
 *   available in the system
 */

#ifndef __DEVICED_PCI_HOST_H__
#define __DEVICED_PCI_HOST_H__

#include <os/osdefs.h>

// Forward declarations
typedef struct PciHost PciHost_t;
struct PciFunctionHandler;

struct PciHostIdentification {
    uuid_t   HostId;
    uint32_t Segment;
    uint8_t  BusStart;
    uint8_t  BusEnd;
};

/**
 * @brief How a host exposes PCI I/O BAR resources to device drivers.
 */
enum PciIoResourcePolicy {
    // Expose I/O BARs as processor I/O port resources.
    PciIoResourcePorts,

    // Translate I/O BARs to memory resources through the host's I/O window.
    PciIoResourceMemory
};

/**
 * @brief Registers a constructed PCI host without scanning its buses or adding devices.
 *
 * Call PciInitialize first. The caller must serialize discovery and teardown.
 * PCI protects request lookups with a lock.
 *
 * @param host Host with configuration access and a valid bus range. The host must
 *             already hold a reference to any firmware data it uses.
 * @return OS_EOK transfers ownership to PCI and assigns a unique host ID that is
 *         not reused. Registration fails if the host is already registered or its
 *         bus range overlaps another host in the same segment. On failure, the
 *         caller still owns the unchanged host and its resources.
 */
__EXTERN oserr_t
PciHostRegister(
    _In_ struct PciHost* host);

/**
 * @brief Initializes PCI once during startup when no other thread is changing
 * the host list. Repeated calls leave hosts and the original handler list unchanged.
 *
 * @param handlers Ordered function handlers; list and entries must outlive PCI.
 *                 NULL is valid when handlerCount is zero.
 * @param handlerCount Number of handlers selected by service startup.
 */
__EXTERN void
PciInitialize(
    _In_ const struct PciFunctionHandler* const* handlers,
    _In_ size_t                                handlerCount);

/**
 * @brief Releases a host after its clients have stopped.
 *
 * Call PciInitialize first and serialize this call with discovery and publication.
 * This also frees a newly constructed host if registration failed, even if it
 * has no root device. The host's controller state, I/O mapping, and firmware data
 * are released; other hosts are unaffected. If removing device-manager entries
 * fails, the host and remaining IDs stay valid so the caller can retry. Controller
 * or mapping cleanup failures also preserve the host and its firmware reference.
 * A registered host keeps its bus range reserved until cleanup completes.
 * Once removal succeeds, publication cannot restart while teardown is pending.
 *
 * @param bus Host to release. NULL requires no cleanup.
 * @return OS_EOK after freeing the host or when bus is NULL. On error, the host
 *         remains allocated and the caller must retry cleanup.
 */
__EXTERN oserr_t
PciHostDestroy(
    _In_ PciHost_t* bus);

/**
 * @brief Returns the host identity and bus range without exposing controller state.
 *
 * @param host Host that remains alive while the returned description is used.
 * @return Pointer to the host's identification structure, unchanged after host
 *         registration. The host owns this structure. Do not free it.
 */
__EXTERN const struct PciHostIdentification*
PciHostGetIdentification(
    _In_ const PciHost_t* host);

#endif

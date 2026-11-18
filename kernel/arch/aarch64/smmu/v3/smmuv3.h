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
 * SMMUv3 native unit interface.
 * An SMMUv3 controller sits between DMA-capable devices and system RAM, using
 * stage-2 page tables to isolate and translate device memory accesses. This
 * header exposes the create, start, poll, and destroy entry points used to
 * bring up one hardware unit. Production boot does not start these units yet.
 */

#ifndef __ARM_SMMUV3_H__
#define __ARM_SMMUV3_H__

#include "../description.h"
#include "../../../../memory/dma/backend.h"

DECL_STRUCT(SMMUv3Unit);

/**
 * @brief Probes an SMMUv3 controller and allocates its software wrapper.
 *
 * Maps the 128 KiB MMIO register window and reads the hardware ID registers to
 * verify that the controller supports stage-2 translation, AArch64 4 KiB page
 * tables, cache-coherent table walks, and two-level stream tables. This call
 * never writes to hardware registers and does not allocate queues or page
 * tables in RAM.
 *
 * Pass *unitOut as NULL. On failure, *unitOut stays NULL. The MMIO register
 * mapping stays active for the lifetime of the kernel device mapping (via
 * Arm64MapDevice).
 *
 * @param description Firmware description of the SMMUv3 controller.
 * @param unitOut     Receives the created unit pointer on success.
 * @return OS_EOK on success, OS_EINVALPARAMS for NULL arguments, OS_EBUSY if
 *         *unitOut is already populated, OS_ENOTSUPPORTED if the register
 *         window or hardware features are unsupported, OS_EOOM on allocation
 *         failure, or an error from generic DMA unit creation.
 */
KERNELABI oserr_t KERNELAPI
SMMUv3UnitCreate(
    _In_    const ArmSMMUDescription_t* description,
    _InOut_ SMMUv3Unit_t**              unitOut);

/**
 * @brief Allocates control memory and enables hardware translation.
 *
 * Allocates the stream table root, command ring, and event ring, then programs
 * the controller registers within a single 100 ms timeout budget. Before
 * calling this, stop all devices that issue DMA through this controller and
 * make sure firmware is no longer accessing its registers.
 *
 * Calling this again on a healthy running unit succeeds immediately. A
 * concurrent call from another thread returns OS_EBUSY. If startup fails part
 * way through, partial hardware state and allocated memory stay in unit so the
 * caller can still call SMMUv3UnitPoll for fault logs and SMMUv3UnitDestroy
 * for cleanup.
 *
 * @param unit Probed unit returned by SMMUv3UnitCreate.
 * @return OS_EOK on success, OS_EINVALPARAMS if unit is NULL, OS_EBUSY if
 *         startup or teardown is already running, OS_ECANCELLED if an earlier
 *         start failed, OS_ETIMEOUT if hardware did not acknowledge within
 *         100 ms, or a memory or fault error.
 */
KERNELABI oserr_t KERNELAPI
SMMUv3UnitStart(
    _In_ SMMUv3Unit_t* unit);

/**
 * @brief Returns the generic DMA IOMMU handle for this SMMUv3 unit.
 *
 * The kernel DMA layer uses this handle to create address spaces and map
 * buffers. While the unit is healthy, space creation returns OS_EBUSY until
 * SMMUv3UnitStart finishes. Never destroy the returned handle directly, because
 * SMMUv3UnitDestroy owns its lifetime.
 *
 * @param unit Native SMMUv3 unit, or NULL.
 * @return Generic DMA IOMMU unit pointer, or NULL if unit is NULL.
 */
KERNELAPI DMAIOMMUUnit_t* KERNELABI
SMMUv3UnitGetBackend(
    _In_ SMMUv3Unit_t* unit);

/**
 * @brief Checks global hardware errors and drains pending fault events.
 *
 * This driver polls in thread context rather than using hardware interrupts.
 * Each call checks for global errors and reads up to one full ring of event
 * records. Any observed event or hardware error marks the generic DMA unit as
 * failed so new DMA work stops. Propagate failures to attached DMA domains
 * after this call returns so domain callbacks never run while the unit mutex
 * is held.
 *
 * @param unit Native SMMUv3 unit to inspect.
 * @return OS_EOK if healthy, OS_EINVALPARAMS if unit is NULL, OS_EBUSY if the
 *         unit has not started, is closing, or failed startup (diagnostics are
 *         still drained after a failed start), or OS_ECANCELLED or a fault
 *         status once the unit has failed.
 */
KERNELABI oserr_t KERNELAPI
SMMUv3UnitPoll(
    _In_ SMMUv3Unit_t* unit);

/**
 * @brief Stops the controller, frees its memory, and clears *unit.
 *
 * All DMA address spaces must already be closed and all device DMA must have
 * stopped before calling this. If the unit was only probed and never started,
 * software state is freed with no clock dependency or register writes.
 *
 * When a hardware wait times out during shutdown, this saves its progress and
 * returns OS_ETIMEOUT so the caller can retry later. If a stalled transaction
 * or queue error prevents proving that hardware stopped reading RAM, control
 * memory stays allocated and OS_ECANCELLED is returned. *unit is cleared to
 * NULL only after hardware, memory, and generic unit cleanup all finish.
 *
 * @param unit Pointer to the unit handle to destroy. Ignored if NULL or *unit
 *             is already NULL.
 * @return OS_EOK on success, OS_EBUSY if spaces are still open or startup is
 *         running, OS_ETIMEOUT if a retryable hardware wait expired, or
 *         OS_ECANCELLED if hardware reliability was lost.
 */
KERNELABI oserr_t KERNELAPI
SMMUv3UnitDestroy(
    _InOut_ SMMUv3Unit_t** unit);

#endif //!__ARM_SMMUV3_H__

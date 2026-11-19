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
 * @brief Checks global hardware errors and reads pending event records.
 *
 * This driver polls in thread context rather than using hardware interrupts.
 * Call without the unit mutex held. Poll takes it and releases it again.
 *
 * The unit writes a 32-byte event record to its event queue for every access
 * it refuses and for every problem it finds in its own work. Poll reads the
 * records and sorts each one by its event number:
 * - Device fault: the record describes an access that a device sent, such as
 *   an address with no mapping or a write to a read-only page. The unit has
 *   already refused and ended that access, so the unit stays healthy. Poll
 *   writes one DMAIOMMUFault_t to faults and does not log it.
 * - Unit failure: the record points at the stream table, a page table or a
 *   queue that the kernel wrote, or at memory the unit reads by itself. A
 *   record with its Stall bit set or with an unknown event number is handled
 *   the same way. The kernel can no longer trust the unit. Poll logs the
 *   event number and Stream ID and fails the unit permanently. No record is
 *   written for it.
 * Any active global error (GERROR) also fails the unit.
 *
 * When the hardware dropped records because the queue was full, Poll
 * acknowledges that and writes one DMAIOMMUFaultReportsLost record first. The
 * unit stays healthy.
 *
 * One call reads at most capacity event records, or capacity - 1 when it also
 * reports an overflow. The rest stay in the hardware queue for the next call.
 * A caller that receives capacity records should poll again. After Poll
 * returns, pass each record to DMAManagerReportFault. Poll never calls it,
 * because that function takes manager locks that must not be taken under the
 * unit mutex.
 *
 * @param unit     Native SMMUv3 unit to inspect.
 * @param faults   Receives up to capacity fault records.
 * @param capacity Number of records faults can hold. Must be at least 1.
 * @param countOut Receives the number of records written. Set on every
 *                 return when it is not NULL.
 * @return OS_EOK while the unit is healthy, also when records were written.
 *         OS_EINVALPARAMS if unit, faults or countOut is NULL or capacity is 0.
 *         OS_EBUSY if the unit has not started, is closing, or failed
 *         startup. After a failed start, events are still read and records
 *         may be written. OS_ECANCELLED once the unit has failed. Records
 *         written in the same call stay valid with every status.
 */
KERNELABI oserr_t KERNELAPI
SMMUv3UnitPoll(
    _In_  SMMUv3Unit_t*    unit,
    _Out_ DMAIOMMUFault_t* faults,
    _In_  uint32_t         capacity,
    _Out_ uint32_t*        countOut);

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

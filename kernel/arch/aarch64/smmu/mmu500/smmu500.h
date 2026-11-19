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
 * MMU-500 native unit interface.
 * Exposes lifecycle controls (create, start, poll, and destroy) and access to
 * the generic DMA IOMMU wrapper for one Arm MMU-500 controller. Production
 * boot does not start these units yet.
 */

#ifndef __ARM_SMMU500_H__
#define __ARM_SMMU500_H__

#include "../description.h"
#include "../../../../memory/dma/backend.h"

// Opaque handle representing one probed or running MMU-500 controller.
DECL_STRUCT(SMMU500Unit);

/**
 * @brief Probes an MMU-500 controller and creates its software unit record.
 *
 * Copies the firmware description, maps the controller's register window, and
 * reads its hardware ID registers to check capabilities and table geometry.
 * Leaves hardware registers untouched and exposes no control memory to the
 * device. Requires safe, powered register access, support for 4 KiB pages,
 * stage-2 address translation, stream matching, and cache-coherent table walks.
 *
 * @param description Firmware description of the MMU-500 controller.
 * @param unitOut     Pointer to a NULL slot that receives the new unit on
 *                    success. Unchanged if creation fails.
 * @return OS_EOK on success, OS_EINVALPARAMS for missing pointers, OS_EBUSY if
 *         *unitOut is already populated, OS_ENOTSUPPORTED if hardware
 *         capabilities or geometry are unsupported, or an allocation/mapping
 *         error. The register mapping stays alive for Arm64MapDevice's lifetime.
 */
KERNELABI oserr_t KERNELAPI
SMMU500UnitCreate(
    _In_    const ArmSMMUDescription_t* description,
    _InOut_ SMMU500Unit_t**             unitOut);

/**
 * @brief Initializes MMU-500 hardware so it can translate DMA traffic.
 *
 * Allocates the control memory pool and programs global fault rules, stream
 * slots, and context banks within a single 100 ms hardware wait budget.
 *
 * Before calling this, stop all DMA sources behind the controller, stop any
 * firmware access to it, and ensure the kernel has exclusive control of its
 * Non-secure context banks and stream match slots. Calling Start on an already
 * ready unit checks and returns its generic health status. If startup fails,
 * partial resources stay inside unit so callers can still inspect faults with
 * SMMU500UnitPoll and clean up with SMMU500UnitDestroy.
 *
 * @param unit Probed unit returned by SMMU500UnitCreate.
 * @return OS_EOK on success, OS_EINVALPARAMS if unit is NULL, OS_EBUSY if
 *         another thread is already starting or tearing down the unit,
 *         OS_ECANCELLED if an earlier startup failed or a hardware write was
 *         rejected, or OS_ETIMEOUT if hardware did not finish within 100 ms.
 */
KERNELABI oserr_t KERNELAPI
SMMU500UnitStart(
    _In_ SMMU500Unit_t* unit);

/**
 * @brief Returns the generic DMA IOMMU unit wrapped by this MMU-500 driver.
 *
 * The returned pointer stays valid as long as unit remains alive. Never pass
 * it directly to DMAIOMMUUnitDestroy. Tear down the controller through
 * SMMU500UnitDestroy instead. Creating address spaces on this backend returns
 * OS_EBUSY until SMMU500UnitStart completes.
 *
 * @param unit MMU-500 unit handle, or NULL.
 * @return Generic DMA IOMMU unit pointer, or NULL if unit is NULL.
 */
KERNELAPI DMAIOMMUUnit_t* KERNELABI
SMMU500UnitGetBackend(
    _In_ SMMU500Unit_t* unit);

/**
 * @brief Reads and clears the fault status of the unit and its context banks.
 *
 * This driver polls in thread context rather than using hardware interrupts.
 * Call without the unit mutex held. Poll takes it and releases it again.
 *
 * The MMU-500 has no event queue. It keeps one fault in the global fault
 * status register (GFSR) for accesses that reach no context bank, and one
 * fault in each context bank's status register (CB_FSR). A context bank is
 * the hardware that translates for one address space. Address and syndrome
 * registers next to each status register describe the fault it holds. A
 * fault that arrives while a status register is nonzero only sets its MULTI
 * bit. Poll reads each status register and sorts the fault it holds:
 * - Device fault: the fault describes an access that a device sent, such as
 *   an address with no mapping (TF), a write to a read-only page (PF) or a
 *   Stream ID that no stream match slot accepts (USF). The unit has already
 *   refused and ended that access, so the unit stays healthy. Poll writes one
 *   DMAIOMMUFault_t to faults, clears the status and does not log it. MULTI
 *   next to a device fault is cleared with it and changes nothing.
 * - Unit failure: the fault points at a page table, stream route or register
 *   that the kernel wrote, or at the unit itself, such as a stalled
 *   transaction (SS) or an external abort (EF). The kernel can no longer
 *   trust the unit. Poll logs the status, address, syndrome, Stream ID and
 *   context bank, fails the unit permanently and clears the status. No
 *   record is written for it.
 *
 * One call writes at most one record for GFSR and one for each context bank.
 * GFSR is read first, then the banks in index order. Once capacity records
 * are written, the remaining banks are not read and keep their status for
 * the next call. A caller that receives capacity records should poll again.
 * After Poll returns, pass each record to DMAManagerReportFault. Poll never
 * calls it, because that function takes manager locks that must not be
 * taken under the unit mutex.
 *
 * @param unit     MMU-500 unit to inspect.
 * @param faults   Receives up to capacity fault records.
 * @param capacity Number of records faults can hold. Must be at least 1.
 * @param countOut Receives the number of records written. Set on every
 *                 return when it is not NULL.
 * @return OS_EOK while the unit is healthy, also when records were written.
 *         OS_EINVALPARAMS if unit, faults or countOut is NULL or capacity is 0.
 *         OS_EBUSY if the unit has not started, is closing, or failed
 *         startup. After a failed start, fault status is still read and
 *         records may be written. OS_ECANCELLED once the unit has failed.
 *         Records written in the same call stay valid with every status.
 */
KERNELABI oserr_t KERNELAPI
SMMU500UnitPoll(
    _In_  SMMU500Unit_t*   unit,
    _Out_ DMAIOMMUFault_t* faults,
    _In_  uint32_t         capacity,
    _Out_ uint32_t*        countOut);

/**
 * @brief Shuts down and frees an MMU-500 unit after DMA traffic stops.
 *
 * Stop all callers and DMA devices behind this controller and destroy every
 * DMA address space before calling this. Live address spaces cause this call
 * to fail with OS_EBUSY.
 *
 * If hardware waits time out during shutdown, returns OS_ETIMEOUT with progress
 * saved so a later call can resume where it stopped. If hardware completion
 * became unreliable earlier, returns OS_ECANCELLED and keeps control memory
 * allocated so hardware can never read freed RAM. A unit that was only probed
 * (never started) is freed in software without touching hardware registers or
 * requiring an active clock. Clears *unit to NULL only after full cleanup.
 *
 * @param unit Address of the unit pointer to destroy. Safe if NULL or *unit is NULL.
 * @return OS_EOK on success, OS_EBUSY if spaces are still open or startup is
 *         running, OS_ETIMEOUT on hardware wait timeout, or OS_ECANCELLED if
 *         control memory must stay allocated.
 */
KERNELABI oserr_t KERNELAPI
SMMU500UnitDestroy(
    _InOut_ SMMU500Unit_t** unit);

#endif //!__ARM_SMMU500_H__

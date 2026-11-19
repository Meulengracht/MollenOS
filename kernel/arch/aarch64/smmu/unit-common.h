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
 * ARM SMMU Common Unit Lifecycle
 * Shared startup, health polling, teardown, and error-latching logic for one
 * SMMU controller. Both SMMUv3 and MMU-500 embed ArmSMMUUnitCommon_t inside
 * their unit struct and pass hardware-specific callbacks into these gates.
 */

#ifndef __ARM_SMMU_UNIT_COMMON_H__
#define __ARM_SMMU_UNIT_COMMON_H__

#include <ds/list.h>
#include <stdatomic.h>

#include "description.h"
#include "memory.h"
#include "../../../memory/dma/backend.h"

// Lifecycle states for a single SMMU hardware unit.
enum ArmSMMUUnitState {
    ArmSMMUUnitProbed,      // Register window mapped. Hardware is untouched.
    ArmSMMUUnitStarting,    // One thread won the startup transition and is configuring hardware.
    ArmSMMUUnitReady,       // Hardware initialized and translating DMA.
    ArmSMMUUnitStartFailed, // Startup failed. Unit cannot serve DMA.
    ArmSMMUUnitClosing      // Teardown has started. Polling and new work are blocked.
};

/**
 * @brief Shared per-controller state embedded in every SMMU backend unit.
 *
 * State may be read atomically without a lock. All other mutable fields must
 * be accessed under Backend's unit mutex, which generic DMA callbacks already
 * hold and native unit helpers acquire directly.
 *
 * CompletionUnreliable acts as a one-way safety switch: if hardware stalls or
 * internal table tracking breaks, setting it prevents Destroy from releasing
 * control memory that the SMMU might still be reading. Zero-initialize the
 * struct before calling ArmSMMUUnitCommonInitialize.
 */
typedef struct ArmSMMUUnitCommon {
    ArmSMMUDescription_t           Description;          // Firmware config (MMIO range, IRQ lines, coherency).
    uintptr_t                      Registers;            // Kernel virtual mapping of the unit's MMIO registers.
    DMAIOMMUUnit_t*                Backend;              // Generic DMA unit holding the lock and unit health.
    _Atomic(enum ArmSMMUUnitState) State;                // Current lifecycle state.
    bool                           CompletionUnreliable; // True when hardware may still access control memory.
    ArmSMMUMemory_t                Memory;               // Pre-allocated control memory pool for tables/queues.
    list_t                         Spaces;               // Active translation address spaces on this unit.
} ArmSMMUUnitCommon_t;

/**
 * @brief Backend callback run inside the unit mutex for startup, polling, or teardown.
 *
 * @param unit Pointer to the backend's enclosing unit struct.
 * @return OS_EOK on success, or an error describing why the hardware step failed.
 */
typedef oserr_t (*ArmSMMUUnitStep_t)(
    _In_ void* unit);

/**
 * @brief Saves the firmware description and maps the SMMU register window.
 *
 * Pass a zeroed common struct. Leaves hardware registers untouched and sets
 * State to ArmSMMUUnitProbed.
 *
 * @param common      Zeroed common unit state to initialize.
 * @param description Firmware description of the SMMU controller.
 * @return OS_EOK on success, or OS_EOOM if mapping the MMIO window fails.
 */
KERNELABI oserr_t KERNELAPI
ArmSMMUUnitCommonInitialize(
    _In_ ArmSMMUUnitCommon_t*        common,
    _In_ const ArmSMMUDescription_t* description);

/**
 * @brief Runs the backend startup callback once and moves the unit to Ready.
 *
 * Uses an atomic compare-and-swap so only one caller transitions from Probed
 * to Starting. If already Ready, returns the generic unit's health status.
 * Sets State to ArmSMMUUnitReady on success or ArmSMMUUnitStartFailed on error.
 *
 * @param common Unit state previously initialized by ArmSMMUUnitCommonInitialize.
 * @param start  Backend hardware initialization routine.
 * @param unit   Backend unit pointer passed to start.
 * @return OS_EOK on success, OS_EBUSY if another thread is starting or tearing
 *         down, OS_ECANCELLED if an earlier start failed, or start's error code.
 */
KERNELABI oserr_t KERNELAPI
ArmSMMUUnitStart(
    _In_ ArmSMMUUnitCommon_t* common,
    _In_ ArmSMMUUnitStep_t    start,
    _In_ void*                unit);

/**
 * @brief Polls controller health and pending faults under the unit mutex.
 *
 * Skips polling while the unit is Probed, Starting, or Closing. If startup
 * failed earlier, still runs poll to inspect/acknowledge hardware faults, but
 * reports OS_EBUSY instead of OS_EOK so a broken controller never reads healthy.
 *
 * @param common Unit state to poll.
 * @param poll   Backend polling routine.
 * @param unit   Pointer passed to poll unchanged. This is the backend unit, or
 *               a backend request that names the unit.
 * @return OS_EOK if healthy and Ready, OS_EBUSY if not started, closing, or
 *         failed at startup, or the error returned by poll.
 */
KERNELABI oserr_t KERNELAPI
ArmSMMUUnitPoll(
    _In_ ArmSMMUUnitCommon_t* common,
    _In_ ArmSMMUUnitStep_t    poll,
    _In_ void*                unit);

/**
 * @brief Tears down the SMMU controller and destroys its generic DMA unit.
 *
 * Refuses teardown with OS_EBUSY while translation spaces are still open or
 * startup is running. If CompletionUnreliable is set or the control memory
 * pool failed, returns OS_ECANCELLED without calling teardown so hardware
 * cannot read freed control pages. If teardown itself returns an error, the
 * caller may retry later. Free backend storage only after OS_EOK.
 *
 * @param common   Unit state to tear down.
 * @param teardown Backend hardware shutdown and memory cleanup routine.
 * @param unit     Backend unit pointer passed to teardown.
 * @return OS_EOK on success, OS_EBUSY if still active, OS_ECANCELLED if control
 *         memory must stay pinned, or the error from teardown or unit destruction.
 */
KERNELABI oserr_t KERNELAPI
ArmSMMUUnitDestroy(
    _In_ ArmSMMUUnitCommon_t* common,
    _In_ ArmSMMUUnitStep_t    teardown,
    _In_ void*                unit);

/**
 * @brief Permanently marks a unit unreliable and fails its generic DMA unit.
 *
 * Hold the unit mutex and log the underlying reason before calling this. Once
 * marked, queue and register operations stop and ArmSMMUUnitDestroy keeps
 * control memory allocated so hardware can never access freed pages.
 *
 * @param common Unit state to mark failed.
 * @param status Failure status to record on the generic DMA unit.
 * @return Returns status unchanged for caller convenience.
 */
KERNELABI oserr_t KERNELAPI
ArmSMMUUnitFail(
    _In_ ArmSMMUUnitCommon_t* common,
    _In_ oserr_t              status);

/**
 * @brief Stage-2 table failure callback wired into ArmSMMUStage2Unit_t.
 *
 * @param context Pointer to the unit's ArmSMMUUnitCommon_t.
 * @param status  Failure status reported by the shared page-table code.
 */
KERNELABI void KERNELAPI
ArmSMMUUnitStage2Fail(
    _In_ void*   context,
    _In_ oserr_t status);

/**
 * @brief Checks whether the unit finished startup and has not begun closing.
 *
 * Safe to call without holding the unit mutex.
 *
 * @param common Unit state to inspect.
 * @return true if State is ArmSMMUUnitReady, false otherwise.
 */
KERNELABI bool KERNELAPI
ArmSMMUUnitIsReady(
    _In_ ArmSMMUUnitCommon_t* common);

#endif //!__ARM_SMMU_UNIT_COMMON_H__

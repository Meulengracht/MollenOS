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
 * Shared timeout helpers for SMMUv3 and MMU-500 hardware waits.
 */

#ifndef __ARM_SMMU_DEADLINE_H__
#define __ARM_SMMU_DEADLINE_H__

#include <os/osdefs.h>

// Cap any single hardware wait at 100 ms (in nanoseconds) so a stalled
// controller fails promptly instead of hanging a kernel thread.
#define ARM_SMMU_TIMEOUT_NS 100000000ULL

/**
 * @brief Tracks how long a polling loop has been waiting for SMMU hardware.
 *
 * Uses wall-clock timer ticks instead of a loop counter so waits last the same
 * real time regardless of CPU speed. Initialize this with ArmSMMUDeadlineBegin.
 */
typedef struct ArmSMMUDeadline {
    uint64_t Start;  // System timer timestamp (in nanoseconds) when waiting started.
    uint64_t Budget; // Maximum wait duration in nanoseconds.
} ArmSMMUDeadline_t;

/**
 * @brief Starts a hardware wait timer after the caller holds the unit mutex.
 *
 * Call this after acquiring the unit mutex so time spent waiting for another
 * CPU to drop the lock does not eat into the hardware timeout. Requires an
 * active system timer so polling cannot spin forever on an uninitialized clock.
 *
 * @param timeoutNs   Maximum wait duration in nanoseconds (1 to ARM_SMMU_TIMEOUT_NS).
 * @param deadlineOut Receives the initialized deadline on success.
 * @return OS_EOK on success, OS_EINVALPARAMS if deadlineOut is NULL or
 *         timeoutNs is out of range, or OS_ENOTSUPPORTED when the system
 *         timer is not running yet.
 */
KERNELABI oserr_t KERNELAPI
ArmSMMUDeadlineBegin(
    _In_  uint64_t           timeoutNs,
    _Out_ ArmSMMUDeadline_t* deadlineOut);

/**
 * @brief Checks whether the wait duration has elapsed.
 *
 * @param deadline Deadline previously initialized by ArmSMMUDeadlineBegin.
 * @return true if the elapsed time has reached or passed Budget, false otherwise.
 */
KERNELABI bool KERNELAPI
ArmSMMUDeadlineExpired(
    _In_ const ArmSMMUDeadline_t* deadline);

#endif //!__ARM_SMMU_DEADLINE_H__

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

#include <component/timer.h>
#include <debug.h>

#include "deadline.h"

oserr_t
ArmSMMUDeadlineBegin(
    _In_  uint64_t           timeoutNs,
    _Out_ ArmSMMUDeadline_t* deadlineOut)
{
    UInteger64_t frequency;
    tick_t       now;

    if (deadlineOut == NULL || timeoutNs == 0 || timeoutNs > ARM_SMMU_TIMEOUT_NS) {
        ERROR("ArmSMMUDeadlineBegin: invalid timeout %llu ns", timeoutNs);
        return OS_EINVALPARAMS;
    }

    // Refuse hardware waits before the platform timer is running, since a zero
    // frequency means timestamps will not advance and polling could hang forever.
    SystemTimerGetClockFrequency(&frequency);
    if (frequency.QuadPart == 0) {
        ERROR("ArmSMMUDeadlineBegin: hardware waits require a running system clock");
        return OS_ENOTSUPPORTED;
    }
    
    SystemTimerGetTimestamp(&now);
    deadlineOut->Start = (uint64_t)now;
    deadlineOut->Budget = timeoutNs;
    return OS_EOK;
}

bool
ArmSMMUDeadlineExpired(
    _In_ const ArmSMMUDeadline_t* deadline)
{
    tick_t now;

    SystemTimerGetTimestamp(&now);

    // Unsigned subtraction stays accurate across a 64-bit counter wraparound
    // as long as the elapsed interval is smaller than the counter range.
    return (uint64_t)now - deadline->Start >= deadline->Budget;
}

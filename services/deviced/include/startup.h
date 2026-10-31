/**
 * Copyright 2026, Philip Meulengracht
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

#ifndef __DEVICED_STARTUP_H__
#define __DEVICED_STARTUP_H__

#include <os/osdefs.h>

typedef enum DmStartupOutcome {
    // No startup work remains, including an absent optional subsystem.
    DmStartupComplete,
    // Another attempt may finish setup or release unfinished resources.
    DmStartupRetry,
    // The current description or implementation cannot complete startup.
    DmStartupFailed
} DmStartupOutcome_t;

typedef struct DmStartupResult {
    oserr_t            Status;
    DmStartupOutcome_t Outcome;
} DmStartupResult_t;

/**
 * @brief Classify a setup status when the device description will not change.
 * Use __DmStartupRetryResult for cleanup errors or a dependency that can appear
 * later, even when the status would otherwise mean startup has failed.
 *
 * @param status Setup error, or OS_EOK after completion.
 * @param result Caller-provided storage for the status and startup outcome.
 */
static inline void
__DmStartupFromStatus(
    _In_  oserr_t            status,
    _Out_ DmStartupResult_t* result)
{
    result->Status = status;

    switch (status) {
        case OS_EOK:
            result->Outcome = DmStartupComplete;
            break;
        case OS_EINVALPARAMS:
        case OS_ENOTSUPPORTED:
        case OS_EEXISTS:
        case OS_EOVERFLOW:
        case OS_ENOENT:
            result->Outcome = DmStartupFailed;
            break;
        default:
            result->Outcome = DmStartupRetry;
            break;
    }
}

/**
 * @brief Request another attempt when cleanup or a dependency check fails.
 *
 * @param status Cleanup or dependency error, or OS_EOK when work completed.
 * @param result Caller-provided storage, retry on failure or complete on success.
 */
static inline void
__DmStartupRetryResult(
    _In_  oserr_t            status,
    _Out_ DmStartupResult_t* result)
{
    result->Status = status;
    result->Outcome = status == OS_EOK ? DmStartupComplete : DmStartupRetry;
}

/**
 * @brief Combine startup results, giving pending retries priority over failure.
 * Completed results are ignored. The first unfinished result supplies Status,
 * even if a later result changes Outcome to DmStartupRetry.
 *
 * @param result Combined result, initially zeroed.
 * @param next Result from another startup owner.
 */
static inline void
__DmStartupMerge(
    _InOut_ DmStartupResult_t*       result,
    _In_    const DmStartupResult_t* next)
{
    if (next->Outcome == DmStartupComplete) {
        return;
    }
    
    if (result->Outcome == DmStartupComplete) {
        *result = *next;
    } else if (next->Outcome == DmStartupRetry) {
        result->Outcome = DmStartupRetry;
    }
}

#endif

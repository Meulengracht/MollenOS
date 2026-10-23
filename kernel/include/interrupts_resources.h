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
 * These helpers map device handlers and provider callbacks for interrupt-time use.
 * The destination table tracks each mapping so callers can retry failed cleanup.
 */

#ifndef __INTERRUPTS_RESOURCES_H__
#define __INTERRUPTS_RESOURCES_H__

#include <ddk/interrupt.h>

/**
 * @brief Check a copied handler resource description before mapping it.
 *
 * @param resources Description of the handler and its resources.
 * @return OS_EOK if valid, or OS_EINVALPARAMS.
 */
oserr_t
InterruptValidateResources(
    _In_ InterruptResourceTable_t* resources);

/**
 * @brief Map a fast handler and its resources for use during interrupts.
 *
 * @param id Binding identity for diagnostics.
 * @param Source Handler resources in the current process.
 * @param Destination Zeroed table that records each mapping. Keep it until
 *        InterruptReleaseResources succeeds.
 * @return OS_EOK on success, or an error. If cleanup leaves mappings behind,
 *         Destination records them so the caller can retry the release.
 */
oserr_t
InterruptResolveResources(
    _In_    uuid_t                    id,
    _In_    InterruptResourceTable_t* Source,
    _InOut_ InterruptResourceTable_t* Destination);

/**
 * @brief Release mappings after handlers have stopped and finished running.
 *
 * @param id Binding identity for diagnostics.
 * @param Resources Mappings to release. Failed releases stay recorded for retry.
 * @return OS_EOK when all mappings are gone, or a retryable cleanup error.
 */
oserr_t
InterruptReleaseResources(
    _In_    uuid_t                    id,
    _InOut_ InterruptResourceTable_t* Resources);
#endif

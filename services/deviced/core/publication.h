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
 *
 * Registers related devices before allowing their drivers to start.
 */

#ifndef __DEVICED_PUBLICATION_H__
#define __DEVICED_PUBLICATION_H__

#include <device-provider.h>

struct __DmPublicationEntry;

enum DmPublicationState {
    DmPublicationCollecting,
    DmPublicationReady,
    DmPublicationBinding,
    DmPublicationRemoving,
    DmPublicationRemoved
};

/**
 * @brief Keeps related device entries in the order they were added.
 * Start with a zeroed group. Add parents before their children, then finish the
 * group before enabling driver binding. Removal follows the opposite order.
 * The owner must keep saved ID locations and provider objects alive until removal
 * finishes, and must not change IDs or remove these entries independently.
 * Calls for one group must be made one at a time, without holding provider locks.
 */
struct DmPublicationGroup {
    struct __DmPublicationEntry* First;
    struct __DmPublicationEntry* Last;
    enum DmPublicationState      State;
};

/**
 * @brief Registers a description without starting its driver and saves its ID.
 * The registry owns the description only on success. The group remembers the ID
 * location and clears it after successful removal; keep that location alive.
 *
 * @param group Group still accepting descriptions.
 * @param registration Description and optional provider to register.
 * @param allowBinding Whether this entry may be matched to a driver later.
 * @param deviceId Receives the ID. Must contain UUID_INVALID before adding.
 * @return OS_EOK on success; failure leaves ownership and the saved ID unchanged.
 */
__EXTERN oserr_t
DmPublicationAdd(
    _InOut_ struct DmPublicationGroup*         group,
    _In_    const struct DmDeviceRegistration* registration,
    _In_    int                                allowBinding,
    _InOut_ uuid_t*                            deviceId);

/**
 * @brief Marks the complete set of descriptions as ready for driver matching.
 * @param group Group that has received every description.
 * @return OS_EOK on success, or OS_EBUSY if adding descriptions has ended.
 */
__EXTERN oserr_t
DmPublicationFinish(
    _InOut_ struct DmPublicationGroup* group);

/**
 * @brief Enables eligible entries, remembering each success for later retries.
 * 
 * @param group Finished group. Removal must not have started.
 * @return OS_EOK on success, OS_EBUSY for an unfinished or removing group, or the
 *         first binding error. An error leaves every description registered.
 */
__EXTERN oserr_t
DmPublicationEnableBinding(
    _InOut_ struct DmPublicationGroup* group);

/**
 * @brief Removes entries in reverse order, clearing IDs only after success.
 * Stop clients first. On failure, keep the group and its owner alive for retry.
 * Once removal starts, adding descriptions and enabling drivers are forbidden.
 *
 * @param group Group to remove, including an incomplete or already removed group.
 * @return OS_EOK when empty, or the first removal error.
 */
__EXTERN oserr_t
DmPublicationRemove(
    _InOut_ struct DmPublicationGroup* group);

/**
 * @brief Allows an entirely removed group to be used again.
 * 
 * @param group Empty group whose previous removal finished successfully.
 * @return OS_EOK on success, or OS_EBUSY while the previous group still exists.
 */
__EXTERN oserr_t
DmPublicationReset(
    _InOut_ struct DmPublicationGroup* group);

#endif

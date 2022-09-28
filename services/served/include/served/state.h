/**
 * Copyright 2022, Philip Meulengracht
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

#ifndef __SERVED_STATE_H__
#define __SERVED_STATE_H__

#include <os/osdefs.h>
#include <ds/list.h>
#include <served/types/application.h>

struct State {
    bool   FirstBoot;
    list_t Applications; // List<struct Application>
};

/**
 * @brief Attempts to load served state from storage.
 * @return
 */
extern oserr_t StateLoad(void);

/**
 * @brief
 * @return
 */
extern oserr_t StateSave(void);

/**
 * @brief Returns the current server state
 * @return
 */
extern struct State* State(void);

/**
 * @brief
 */
extern void StateLock(void);

/**
 * @brief
 */
extern void StateUnlock(void);

#endif //!__SERVED_STATE_H__

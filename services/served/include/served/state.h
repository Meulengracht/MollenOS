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

struct State {

};

/**
 * @brief Attempts to load served state from storage.
 * @return
 */
extern oserr_t StateLoad(void);

/**
 * @brief Initializes the served state. This will in essence create a new state
 * for served, and thus if this should be called when one exists, will be overwritten.
 * @return
 */
extern oserr_t StateInitialize(void);



#endif //!__SERVED_STATE_H__

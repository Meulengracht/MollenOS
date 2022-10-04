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

#ifndef __SERVED_SERVER_H__
#define __SERVED_SERVER_H__

#include <os/osdefs.h>

/**
 * @brief
 * @return
 */
extern oserr_t ServerEnsurePaths(void);

/**
 * @brief Before loading server the state should have been either loaded or initialized.
 * @return
 */
extern oserr_t ServerLoad(void);

#endif //!__SERVED_SERVER_H__

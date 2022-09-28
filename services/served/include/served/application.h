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

#ifndef __SERVED_APPLICATION_H__
#define __SERVED_APPLICATION_H__

#include <served/types/application.h>

/**
 * @brief
 * @param application
 * @return
 */
extern oserr_t ApplicationMount(struct Application* application);

/**
 *
 * @param application
 * @return
 */
extern oserr_t ApplicationUnmount(struct Application* application);

/**
 *
 * @param application
 * @return
 */
extern oserr_t ApplicationStartServices(struct Application* application);

/**
 *
 * @param application
 * @return
 */
extern oserr_t ApplicationStopServices(struct Application* application);

#endif //!__SERVED_APPLICATION_H__

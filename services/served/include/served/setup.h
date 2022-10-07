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

#ifndef __SERVED_SETUP_H__
#define __SERVED_SETUP_H__

/**
 * @brief Initializes the served server. This will take care of system setup and initialize
 * all installed applications. Served will wait for /data to be present, and if /data/served
 * is present, will load the system or initialize the system for first run.
 */
extern void served_server_setup_job(void*, void*);

#endif //!__SERVED_SETUP_H__

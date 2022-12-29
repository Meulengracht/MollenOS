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
 */

#ifndef __PHOENIX_DISCOVER_H__
#define __PHOENIX_DISCOVER_H__

#include <ds/mstring.h>
#include <ds/list.h>

struct SystemService {
    int        ID;
    mstring_t* Name;
    mstring_t* Path;
    mstring_t* APIPath;
    // Dependencies is a list of service names this service requires
    // before it is spawned.
    list_t Dependencies;
};

/**
 * @brief Bootstraps the entire system by parsing ramdisk for system services.
 */
extern void PSBootstrap(void*, void*);

/**
 * @brief Cleans up any resources related to bootstrapping. This includes the mapped
 * ramdisk, which should only be cleaned up on process exit.
 */
extern void PSBootstrapCleanup(void);

/**
 * @brief
 * @param systemService
 * @param yaml
 * @param length
 * @return
 */
oserr_t
PSParseServiceYAML(
        _In_ struct SystemService* systemService,
        _In_ const uint8_t*        yaml,
        _In_ size_t                length);

#endif //!__PHOENIX_DISCOVER_H__

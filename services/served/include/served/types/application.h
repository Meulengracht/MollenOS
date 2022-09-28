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

#ifndef __SERVED_TYPE_APPLICATION_H__
#define __SERVED_TYPE_APPLICATION_H__

#include <ds/mstring.h>
#include <ds/list.h>

struct Command {
    element_t  ListHeader;
    mstring_t* Name;
    mstring_t* Path;
    mstring_t* Arguments;
    int        Type;
};

struct Application {
    element_t  ListHeader;
    mstring_t* Name; // publisher/package
    mstring_t* Publisher;
    mstring_t* Package;
    int        Major;
    int        Minor;
    int        Patch;
    int        Revision;
    list_t     Commands; // List<struct Command>
};

/**
 *
 * @param name
 * @param publisher
 * @param package
 * @param major
 * @param minor
 * @param patch
 * @param revision
 * @return
 */
extern struct Application*
ApplicationNew(
        _In_ mstring_t* name,
        _In_ mstring_t* publisher,
        _In_ mstring_t* package,
        _In_ int        major,
        _In_ int        minor,
        _In_ int        patch,
        _In_ int        revision);

/**
 * @brief
 * @param application
 */
extern void
ApplicationDelete(
        _In_ struct Application* application);

/**
 *
 * @param name
 * @param path
 * @param arguments
 * @param type
 * @return
 */
extern struct Command*
CommandNew(
        _In_ mstring_t* name,
        _In_ mstring_t* path,
        _In_ mstring_t* arguments,
        _In_ int        type);

/**
 * @brief
 * @param command
 */
extern void
CommandDelete(
        _In_ struct Command* command);

#endif //!__SERVED_TYPE_APPLICATION_H__

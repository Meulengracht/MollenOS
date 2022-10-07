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
 * Source: https://github.com/Meulengracht/bake/blob/main/libs/platform/include/chef/package.h
 */

#ifndef __CHEF_PACKAGE_H__
#define __CHEF_PACKAGE_H__

#include <stddef.h>

enum chef_package_type {
    CHEF_PACKAGE_TYPE_UNKNOWN,
    CHEF_PACKAGE_TYPE_TOOLCHAIN,
    CHEF_PACKAGE_TYPE_INGREDIENT,
    CHEF_PACKAGE_TYPE_APPLICATION
};

struct chef_version {
    int         major;
    int         minor;
    int         patch;
    int         revision;
    const char* tag;
    long long   size;
    const char* created;
};

struct chef_channel {
    const char*         name;
    struct chef_version current_version;
};

struct chef_architecture {
    const char*          name;
    struct chef_channel* channels;
    size_t               channels_count;
};

struct chef_platform {
    const char*               name;
    struct chef_architecture* architectures;
    size_t                    architectures_count;
};

enum chef_command_type {
    CHEF_COMMAND_TYPE_UNKNOWN,
    CHEF_COMMAND_TYPE_EXECUTABLE,
    CHEF_COMMAND_TYPE_DAEMON
};

struct chef_command {
    enum chef_command_type type;
    const char*            name;
    const char*            description;
    const char*            arguments;
    const char*            path;
    const void*            icon_buffer;
};

struct chef_package {
    const char* platform;
    const char* arch;
    const char* publisher;
    const char* package;
    const char* summary;
    const char* description;
    const char* homepage;
    const char* license;
    const char* eula;
    const char* maintainer;
    const char* maintainer_email;

    enum chef_package_type type;

    struct chef_platform*  platforms;
    size_t                 platforms_count;
};

/**
 * @brief
 *
 * @param[In]       path
 * @param[Out, Opt] packageOut
 * @param[Out, Opt] versionOut
 * @param[Out, Opt] commandsOut     A pointer to a chef_command*, it will be set to an array of commands.
 * @param[Out, Opt] commandCountOut Will be set to the size of commandsOut.
 * @return int
 */
extern int chef_package_load(
        const char* path,
        struct chef_package** packageOut,
        struct chef_version** versionOut,
        struct chef_command** commandsOut,
        int*                  commandCountOut
);

/**
 * @brief Cleans up any resources allocated by the package.
 *
 * @param[In] package A pointer to the package that will be freed.
 */
extern void chef_package_free(struct chef_package* package);

/**
 * @brief Cleans up any resources allocated by the version structure. This is only neccessary
 * to call if the verison was allocated seperately (by chef_package_load).
 *
 * @param[In] version A pointer to the version that will be freed.
 */
extern void chef_version_free(struct chef_version* version);

/**
 * @brief Cleans up any resources allocated by chef_package_load. The commands pointer will pont
 * to an array of struct chef_command, and the caller must save the count as well to pass to this
 * function.
 *
 * @param[In] commands A pointer to an array of commands.
 * @param[In] count    The size of the array passed.
 */
extern void chef_commands_free(struct chef_command* commands, int count);

#endif //!__CHEF_PACKAGE_H__

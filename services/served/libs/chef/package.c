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
 * Source: https://github.com/Meulengracht/bake/blob/main/libs/platform/package.c
 */

#include <chef/package.h>
#include <chef/utils_vafs.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

static struct VaFsGuid g_headerGuid   = CHEF_PACKAGE_HEADER_GUID;
static struct VaFsGuid g_versionGuid  = CHEF_PACKAGE_VERSION_GUID;
static struct VaFsGuid g_commandsGuid = CHEF_PACKAGE_APPS_GUID;

static int __load_package_header(struct VaFs* vafs, struct chef_package** packageOut)
{
    struct chef_vafs_feature_package_header* header;
    struct chef_package*                     package;
    char*                                    data;
    int                                      status;

    status = vafs_feature_query(vafs, &g_headerGuid, (struct VaFsFeatureHeader**)&header);
    if (status != 0) {
        return status;
    }

    package = (struct chef_package*)malloc(sizeof(struct chef_package));
    if (package == NULL) {
        return -1;
    }
    memset(package, 0, sizeof(struct chef_package));

    data = (char*)header + sizeof(struct chef_vafs_feature_package_header);

    package->type = header->type;

#define READ_IF_PRESENT(__MEM) if (header->__MEM ## _length > 0) { \
        package->__MEM = strndup(data, header->__MEM ## _length); \
        data += header->__MEM ## _length; \
    }

    READ_IF_PRESENT(platform)
    READ_IF_PRESENT(arch)
    READ_IF_PRESENT(package)
    READ_IF_PRESENT(summary)
    READ_IF_PRESENT(description)
    READ_IF_PRESENT(homepage)
    READ_IF_PRESENT(license)
    READ_IF_PRESENT(eula)
    READ_IF_PRESENT(maintainer)
    READ_IF_PRESENT(maintainer_email)

#undef READ_IF_PRESENT

    *packageOut = package;
    return 0;
}

static int __load_package_version(struct VaFs* vafs, struct chef_version** versionOut)
{
    struct chef_vafs_feature_package_version* header;
    struct chef_version*                      version;
    char*                                     data;
    int                                       status;

    status = vafs_feature_query(vafs, &g_versionGuid, (struct VaFsFeatureHeader**)&header);
    if (status != 0) {
        return status;
    }

    version = (struct chef_version*)malloc(sizeof(struct chef_version));
    if (version == NULL) {
        return -1;
    }
    memset(version, 0, sizeof(struct chef_version));

    data = (char*)header + sizeof(struct chef_vafs_feature_package_version);

    version->major = header->major;
    version->minor = header->minor;
    version->patch = header->patch;
    version->revision = header->revision;

    if (header->tag_length) {
        version->tag = strndup(data, header->tag_length);
    }

    *versionOut = version;
    return 0;
}

static void __fill_command(char** dataPointer, struct chef_command* command)
{
    struct chef_vafs_package_app* entry = (struct chef_vafs_package_app*)*dataPointer;

    command->type = (enum chef_command_type)entry->type;

    // move datapointer up to the rest of the data
    *dataPointer += sizeof(struct chef_vafs_package_app);

#define READ_IF_PRESENT(__MEM) if (entry->__MEM ## _length > 0) { \
        command->__MEM = strndup(*dataPointer, entry->__MEM ## _length); \
        *dataPointer += entry->__MEM ## _length; \
    }

    READ_IF_PRESENT(name)
    READ_IF_PRESENT(description)
    READ_IF_PRESENT(arguments)
    READ_IF_PRESENT(path)

    // TOOD skip icon for now, we haven't completed support for this
    // on linux yet
    *dataPointer += entry->icon_length;
#undef READ_IF_PRESENT
}

static int __load_package_commands(struct VaFs* vafs, struct chef_command** commandsOut, int* commandCountOut)
{
    struct chef_vafs_feature_package_apps* header;
    struct chef_command*                   commands;
    char*                                  data;
    int                                    status;

    status = vafs_feature_query(vafs, &g_commandsGuid, (struct VaFsFeatureHeader**)&header);
    if (status != 0) {
        return status;
    }

    if (header->apps_count == 0) {
        return -1;
    }

    commands = (struct chef_command*)calloc(header->apps_count, sizeof(struct chef_command));
    if (commands == NULL) {
        return -1;
    }

    data = (char*)header + sizeof(struct chef_vafs_feature_package_apps);
    for (int i = 0; i < header->apps_count; i++) {
        __fill_command(&data, &commands[i]);
    }

    *commandsOut     = commands;
    *commandCountOut = header->apps_count;
    return 0;
}

int chef_package_load(
        const char*           path,
        struct chef_package** packageOut,
        struct chef_version** versionOut,
        struct chef_command** commandsOut,
        int*                  commandCountOut)
{
    struct VaFs* vafs;
    int          status;

    if (path == NULL) {
        errno = EINVAL;
        return -1;
    }

    status = vafs_open_file(path, &vafs);
    if (status != 0) {
        return status;
    }

    if (packageOut) {
        status = __load_package_header(vafs, packageOut);
        if (status != 0) {
            vafs_close(vafs);
            return status;
        }
    }

    if (versionOut) {
        status = __load_package_version(vafs, versionOut);
        if (status != 0) {
            // This is a required header, so something is definitely off
            // lets cleanup
            if (packageOut) {
                chef_package_free(*packageOut);
            }
            vafs_close(vafs);
            return status;
        }
    }

    if (commandsOut && commandCountOut) {
        // This header is optional, which means we won't ever fail on it. If
        // the loader/locate returns error, we zero the out values
        status = __load_package_commands(vafs, commandsOut, commandCountOut);
        if (status != 0) {
            *commandsOut     = NULL;
            *commandCountOut = 0;
        }
    }

    vafs_close(vafs);
    return 0;
}

static void __free_version(struct chef_version* version)
{
    free((void*)version->tag);
}

static void __free_channel(struct chef_channel* channel)
{
    free((void*)channel->name);
    __free_version(&channel->current_version);
}

static void __free_architecture(struct chef_architecture* architecture)
{
    free((void*)architecture->name);
    if (architecture->channels != NULL) {
        for (size_t i = 0; i < architecture->channels_count; i++) {
            __free_channel(&architecture->channels[i]);
        }
        free(architecture->channels);
    }
}

static void __free_platform(struct chef_platform* platform)
{
    free((void*)platform->name);
    if (platform->architectures != NULL) {
        for (size_t i = 0; i < platform->architectures_count; i++) {
            __free_architecture(&platform->architectures[i]);
        }
        free(platform->architectures);
    }
}

void chef_package_free(struct chef_package* package)
{
    if (package == NULL) {
        return;
    }

    free((void*)package->publisher);
    free((void*)package->package);
    free((void*)package->description);
    free((void*)package->homepage);
    free((void*)package->license);
    free((void*)package->maintainer);
    free((void*)package->maintainer_email);

    if (package->platforms != NULL) {
        for (size_t i = 0; i < package->platforms_count; i++) {
            __free_platform(&package->platforms[i]);
        }
        free(package->platforms);
    }
    free(package);
}

void chef_version_free(struct chef_version* version)
{
    if (version == NULL) {
        return;
    }

    __free_version(version);
    free(version);
}

void chef_commands_free(struct chef_command* commands, int count)
{
    if (commands == NULL || count == 0) {
        return;
    }

    for (int i = 0; i < count; i++) {
        free((void*)commands[i].name);
        free((void*)commands[i].path);
        free((void*)commands[i].arguments);
        free((void*)commands[i].description);
        free((void*)commands[i].icon_buffer);
    }
    free(commands);
}

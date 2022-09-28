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
 * Source: https://github.com/Meulengracht/bake/blob/main/libs/platform/include/chef/utils_vafs.h
 */

#ifndef __CHEF_UTILS_VAFS_H__
#define __CHEF_UTILS_VAFS_H__

#include <stddef.h>
#include <chef/package.h>
#include <stdint.h>
#include <vafs/vafs.h>

#define CHEF_PACKAGE_VERSION 0x00010000

#define CHEF_PACKAGE_HEADER_GUID  { 0x91C48A1D, 0xC445, 0x4607, { 0x95, 0x98, 0xFE, 0x73, 0x49, 0x1F, 0xD3, 0x7E } }
#define CHEF_PACKAGE_VERSION_GUID { 0x478ED773, 0xAA23, 0x45DA, { 0x89, 0x23, 0x9F, 0xCE, 0x5F, 0x2E, 0xCB, 0xED } }
#define CHEF_PACKAGE_ICON_GUID    { 0xDB6981BF, 0xC344, 0x47F5, { 0xB6, 0xE1, 0x5C, 0x3C, 0x76, 0xF5, 0x6F, 0xFF } }
#define CHEF_PACKAGE_APPS_GUID    { 0xBE0B9C0E, 0x78D0, 0x45B9, { 0xBA, 0xF9, 0x51, 0xC8, 0x0B, 0x8D, 0x46, 0xC9 } }

struct chef_vafs_feature_package_header {
    struct VaFsFeatureHeader header;

    uint32_t                 version;
    enum chef_package_type   type;

    // lengths of the data for each string, none of the strings
    // are zero terminated, which must be added at load
    uint32_t                 platform_length;
    uint32_t                 arch_length;
    uint32_t                 package_length;
    uint32_t                 summary_length;
    uint32_t                 description_length;
    uint32_t                 homepage_length;
    uint32_t                 license_length;
    uint32_t                 eula_length;
    uint32_t                 maintainer_length;
    uint32_t                 maintainer_email_length;
};

struct chef_vafs_feature_package_version {
    struct VaFsFeatureHeader header;
    int                      major;
    int                      minor;
    int                      patch;
    int                      revision;

    // the data is not zero terminated.
    uint32_t                 tag_length;
};

struct chef_vafs_feature_package_icon {
    struct VaFsFeatureHeader header;
};

struct chef_vafs_feature_package_apps {
    struct VaFsFeatureHeader header;
    int                      apps_count;
};

struct chef_vafs_package_app {
    uint32_t name_length;
    uint32_t description_length;
    uint32_t arguments_length;
    int      type;
    uint32_t path_length;
    uint32_t icon_length;
};

#endif //!__CHEF_UTILS_VAFS_H__

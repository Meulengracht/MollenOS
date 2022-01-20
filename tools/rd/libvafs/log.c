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
 * Vali Initrd Filesystem
 * - Contains the implementation of the Vali Initrd Filesystem.
 *   This filesystem is used to store the initrd of the kernel.
 */

#include "private.h"
#include <stdarg.h>
#include <stdio.h>

static enum VaFsLogLevel g_loglevel = VaFsLogLevel_Warning;

void vafs_log_initalize(
    enum VaFsLogLevel level)
{
    g_loglevel = level;
}

void vafs_log_message(
    enum VaFsLogLevel level,
    const char*       format,
    ...)
{
    va_list args;

    if (level > g_loglevel) {
        return;
    }

    va_start(args, format);
    if (level == VaFsLogLevel_Error) {
        vfprintf(stderr, format, args);
    }
    else {
        vfprintf(stdout, format, args);
    }
    va_end(args);
}

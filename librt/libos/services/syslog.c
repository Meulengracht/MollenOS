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

#include <ddk/service.h>
#include <gracht/link/vali.h>
#include <internal/_syscalls.h>
#include <internal/_utils.h>
#include <os/services/syslog.h>
#include <stdio.h>

#include <sys_log_service_client.h>

void
OSSystemLog(
        _In_ enum OSSysLogLevel level,
        _In_ const char*        fmt, ...)
{
    struct vali_link_message msg = VALI_MSG_INIT_HANDLE(GetSysLogService());
    va_list                  args;
    char                     buffer[256];

    va_start(args, fmt);
    vsnprintf(&buffer[0], sizeof(buffer) - 1, fmt, args);
    va_end(args);

    // During early startup phase, the syslog service has not yet
    // started up and migrated logs, and we resort to the kernel log
    // which only outputs on serial.
    if (msg.address.Data.Handle == UUID_INVALID) {
        (void)Syscall_Debug(level, &buffer[0]);
        return;
    }
    sys_log_log(
            GetGrachtClient(),
            &msg.base,
            (const enum sys_log_level)level,
            &buffer[0]
    );
}

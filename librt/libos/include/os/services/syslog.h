/**
 * Copyright 2023, Philip Meulengracht
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

#ifndef __OS_SERVICE_SYSLOG_H__
#define __OS_SERVICE_SYSLOG_H__

#include <os/osdefs.h>
#include <os/types/syslog.h>

_CODE_BEGIN

/**
 * @brief Logs a message to the system log
 * @param level
 * @param fmt
 */
CRTDECL(void,
OSSystemLog(
        _In_ enum OSSysLogLevel level,
        _In_ const char*        fmt, ...));

_CODE_END
#endif //!__OS_SERVICE_SYSLOG_H__

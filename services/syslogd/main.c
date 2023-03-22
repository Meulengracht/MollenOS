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

#define __TRACE

#include <ddk/service.h>
#include <gracht/link/vali.h>
#include <internal/_utils.h>
#include "console/screen.h"

#include <sys_log_service_server.h>

void ServiceInitialize(
        _In_ struct ServiceStartupOptions* startupOptions)
{
    // Register supported interfaces
    gracht_server_register_protocol(startupOptions->Server, &sys_log_server_protocol);

    // Initialize systems
    LogInitialize();
}

void sys_log_report_stage_invocation(struct gracht_message* message, const char* stage, const int steps)
{

}

void sys_log_report_progress_invocation(struct gracht_message* message, const char* progress)
{

}

void sys_log_log_invocation(struct gracht_message* message, const enum sys_log_level level, const char* msg)
{
    _CRT_UNUSED(message);
    LogPrint((enum OSSysLogLevel)level, msg);
}

void sys_log_inhibit_invocation(struct gracht_message* message)
{
    _CRT_UNUSED(message);
    LogInhibite();
}

void sys_log_uninhibit_invocation(struct gracht_message* message)
{
    _CRT_UNUSED(message);
    LogUninhibite();
}

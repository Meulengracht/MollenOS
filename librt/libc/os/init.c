/**
 * MollenOS
 *
 * Copyright 2018, Philip Meulengracht
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
 *
 * C-Support Initialize Implementation
 * - Definitions, prototypes and information needed.
 */

//#define __TRACE

#include <assert.h>
#include <ddk/utils.h>
#include <internal/_ipc.h>
#include <internal/_io.h>
#include <internal/_syscalls.h>
#include <internal/_utils.h>
#include <os/types/process.h>
#include <stdlib.h>
#include <threads.h>
#include "../threads/tls.h"

extern void StdioInitialize(void);
extern void StdioConfigureStandardHandles(void* inheritanceBlock);
extern void StdSignalInitialize(void);
extern void StdSoInitialize(void);

// The default inbuilt client for rpc communication. In general this should only be used
// internally for calls to services and modules.
static gracht_client_t*         g_gclient     = NULL;
static struct gracht_link_vali* g_gclientLink = NULL;

static char   g_rawCommandLine[1024] = { 0 };
static int    g_isPhoenix            = 0;
static UUId_t g_processId            = UUID_INVALID;

static void
__mark_iod_priority(int iod)
{
    stdio_handle_t* handle = stdio_handle_get(iod);
    if (!handle) {
        return;
    }
    stdio_handle_flag(handle, WX_PRIORITY);
}

void __crt_process_initialize(
        _In_ int                          isPhoenix,
        _In_ ProcessStartupInformation_t* startupInformation)
{
    gracht_client_configuration_t clientConfig;
    struct vali_link_message      msg = VALI_MSG_INIT_HANDLE(GetProcessService());
    OsStatus_t                    osStatus;
    int                           status;
    TRACE("__crt_process_initialize(isPhoenix=%i)", );
    
    // We must set IsModule before anything
    g_isPhoenix = isPhoenix;

    // Initialize the standard C library
    TRACE("__crt_process_initialize initializing stdio");
    StdioInitialize();
    TRACE("__crt_process_initialize initializing stdsig");
    StdSignalInitialize();
    TRACE("__crt_process_initialize initializing so");
    StdSoInitialize();

    // initialite the ipc link
    TRACE("__crt_process_initialize creating rpc link");
    status = gracht_link_vali_create(&g_gclientLink);
    if (status) {
        ERROR("__crt_process_initialize gracht_link_vali_create failed %i", status);
        _Exit(status);
    }

    // configure the client
    gracht_client_configuration_init(&clientConfig);
    gracht_client_configuration_set_link(&clientConfig, (struct gracht_link*)g_gclientLink);

    TRACE("__crt_process_initialize creating rpc client");
    status = gracht_client_create(&clientConfig, &g_gclient);
    if (status) {
        ERROR("__crt_process_initialize gracht_link_vali_client_create failed %i", status);
        _Exit(status);
    }

    TRACE("__crt_process_initialize connecting client");
    status = gracht_client_connect(g_gclient);
    if (status) {
        ERROR("__crt_process_initialize gracht_client_connect failed %i", status);
        _Exit(status);
    }

    // mark the client priority as we need the client for shutting
    // down during crt finalizing
    __mark_iod_priority(gracht_client_iod(g_gclient));
    
    // Get startup information
    TRACE("__crt_process_initialize receiving startup configuration");
    if (isPhoenix) {
        // for phoenix, we have no booter or environment
    }
    else {
        UUId_t dmaHandle = tls_current()->transfer_buffer.handle;
        char*  dmaBuffer = tls_current()->transfer_buffer.buffer;
        size_t maxLength = tls_current()->transfer_buffer.length;

        sys_process_get_startup_information(GetGrachtClient(), &msg.base, thrd_current(), dmaHandle, maxLength);
        gracht_client_wait_message(GetGrachtClient(), &msg.base, GRACHT_MESSAGE_BLOCK);
        sys_process_get_startup_information_result(GetGrachtClient(), &msg.base,
                                                   &osStatus, &g_processId, &startupInformation->ArgumentsLength,
                                                   &startupInformation->InheritationLength, &startupInformation->LibraryEntriesLength);
        assert(osStatus == OsSuccess);

        TRACE("[init] args-len %" PRIuIN ", inherit-len %" PRIuIN ", modules-len %" PRIuIN,
              startupInformation->ArgumentsLength,
              startupInformation->InheritationLength,
              startupInformation->LibraryEntriesLength);
        assert((startupInformation->ArgumentsLength + startupInformation->InheritationLength +
                startupInformation->LibraryEntriesLength) < maxLength);
        
        // fixup pointers
        startupInformation->Arguments = &dmaBuffer[0];
        if (startupInformation->InheritationLength) {
            startupInformation->Inheritation = &dmaBuffer[startupInformation->ArgumentsLength];
        }
        else {
            startupInformation->Inheritation = NULL;
        }

        startupInformation->LibraryEntries = &dmaBuffer[
                startupInformation->ArgumentsLength + startupInformation->InheritationLength];

        // copy the arguments to the raw cmdline
        memcpy(&g_rawCommandLine[0], startupInformation->Arguments, startupInformation->ArgumentsLength);
    }

    // Parse the configuration information
    StdioConfigureStandardHandles(startupInformation->Inheritation);
}

int __crt_is_phoenix(void)
{
    return g_isPhoenix;
}

UUId_t* __crt_processid_ptr(void)
{
    return &g_processId;
}

const char* __crt_cmdline(void)
{
    return &g_rawCommandLine[0];
}

gracht_client_t* GetGrachtClient(void)
{
    return g_gclient;
}

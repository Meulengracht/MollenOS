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
 * along with this program.If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * C-Support Initialize Implementation
 * - Definitions, prototypes and information needed.
 */

//#define __TRACE

#include <assert.h>
#include <ddk/utils.h>
#include <internal/_ipc.h>
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
static gracht_client_t*         g_gclient = NULL;
static struct gracht_link_vali* g_gclientLink = NULL;

static char   g_rawCommandLine[1024] = { 0 };
static int    g_isModule             = 0;
static UUId_t g_processId            = UUID_INVALID;

void InitializeProcess(int IsModule, ProcessStartupInformation_t* StartupInformation)
{
    gracht_client_configuration_t clientConfig;
    struct vali_link_message      msg = VALI_MSG_INIT_HANDLE(GetProcessService());
    OsStatus_t                    osStatus;
    int                           status;
    TRACE("[InitializeProcess]");
    
    // We must set IsModule before anything
    g_isModule = IsModule;

    // Initialize the standard C library
    TRACE("[InitializeProcess] initializing stdio");
    StdioInitialize();
    TRACE("[InitializeProcess] initializing stdsig");
    StdSignalInitialize();
    TRACE("[InitializeProcess] initializing so");
    StdSoInitialize();

    // initialite the ipc link
    TRACE("[InitializeProcess] creating rpc link");
    status = gracht_link_vali_create(&g_gclientLink);
    if (status) {
        ERROR("[InitializeProcess] gracht_link_vali_create failed %i", status);
        _Exit(status);
    }

    // configure the client
    gracht_client_configuration_init(&clientConfig);
    gracht_client_configuration_set_link(&clientConfig, (struct gracht_link*)g_gclientLink);

    TRACE("[InitializeProcess] creating rpc client");
    status = gracht_client_create(&clientConfig, &g_gclient);
    if (status) {
        ERROR("[InitializeProcess] gracht_link_vali_client_create failed %i", status);
        _Exit(status);
    }

    TRACE("[InitializeProcess] connecting client");
    status = gracht_client_connect(g_gclient);
    if (status) {
        ERROR("[InitializeProcess] gracht_client_connect failed %i", status);
        _Exit(status);
    }
    
    // Get startup information
    TRACE("[InitializeProcess] receiving startup configuration");
    if (IsModule) {
        Syscall_ModuleGetStartupInfo(StartupInformation, &g_processId, &g_rawCommandLine[0],
                                     sizeof(g_rawCommandLine));
    }
    else {
        UUId_t dmaHandle = tls_current()->transfer_buffer.handle;
        char*  dmaBuffer = tls_current()->transfer_buffer.buffer;
        size_t maxLength = tls_current()->transfer_buffer.length;

        sys_process_get_startup_information(GetGrachtClient(), &msg.base, thrd_current(), dmaHandle, maxLength);
        gracht_client_wait_message(GetGrachtClient(), &msg.base, GRACHT_MESSAGE_BLOCK);
        sys_process_get_startup_information_result(GetGrachtClient(), &msg.base,
                                                   &osStatus, &g_processId, &StartupInformation->ArgumentsLength,
                                                   &StartupInformation->InheritationLength, &StartupInformation->LibraryEntriesLength);
        assert(osStatus == OsSuccess);

        TRACE("[init] args-len %" PRIuIN ", inherit-len %" PRIuIN ", modules-len %" PRIuIN,
            StartupInformation->ArgumentsLength,
            StartupInformation->InheritationLength,
            StartupInformation->LibraryEntriesLength);
        assert((StartupInformation->ArgumentsLength + StartupInformation->InheritationLength + 
            StartupInformation->LibraryEntriesLength) < maxLength);
        
        // fixup pointers
        StartupInformation->Arguments = &dmaBuffer[0];
        if (StartupInformation->InheritationLength) {
            StartupInformation->Inheritation = &dmaBuffer[StartupInformation->ArgumentsLength];
        }
        else {
            StartupInformation->Inheritation = NULL;
        }
        
        StartupInformation->LibraryEntries = &dmaBuffer[
            StartupInformation->ArgumentsLength + StartupInformation->InheritationLength];

        // copy the arguments to the raw cmdline
        memcpy(&g_rawCommandLine[0], StartupInformation->Arguments, StartupInformation->ArgumentsLength);
    }

    // Parse the configuration information
    StdioConfigureStandardHandles(StartupInformation->Inheritation);
}

int IsProcessModule(void)
{
    return g_isModule;
}

UUId_t* GetInternalProcessId(void)
{
    return &g_processId;
}

const char* GetInternalCommandLine(void)
{
    return &g_rawCommandLine[0];
}

gracht_client_t* GetGrachtClient(void)
{
    return g_gclient;
}

/* MollenOS
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
#include <stdlib.h>
#include <threads.h>
#include "../threads/tls.h"

extern void StdioInitialize(void);
extern void StdioConfigureStandardHandles(void* inheritanceBlock);
extern void StdSignalInitialize(void);
extern void StdSoInitialize(void);

// The default inbuilt client for rpc communication. In general this should only be used
// internally for calls to services and modules.
static char             __crt_gclient_buffer[GRACHT_MAX_MESSAGE_SIZE] = { 0 };
static gracht_client_t* __crt_gclient = NULL;

static char   __crt_raw_cmdline[1024] = { 0 };
static int    __crt_is_module         = 0;
static UUId_t __crt_process_id        = UUID_INVALID;

void InitializeProcess(int IsModule, ProcessStartupInformation_t* StartupInformation)
{
    gracht_client_configuration_t clientConfig = { 0 };
    struct vali_link_message      msg          = VALI_MSG_INIT_HANDLE(GetProcessService());
    OsStatus_t                    osStatus;
    int                           status;
    TRACE("[InitializeProcess]");
    
    // We must set IsModule before anything
    __crt_is_module = IsModule;

    // Initialize the standard C library
    TRACE("[InitializeProcess] initializing stdio");
    StdioInitialize();
    TRACE("[InitializeProcess] initializing stdsig");
    StdSignalInitialize();
    TRACE("[InitializeProcess] initializing so");
    StdSoInitialize();

    // Create the ipc client
    TRACE("[InitializeProcess] creating rpc link");
    status = gracht_link_vali_client_create(&clientConfig.link);
    if (status) {
        ERROR("[InitializeProcess] gracht_link_vali_client_create failed %i", status);
        _Exit(status);
    }

    TRACE("[InitializeProcess] creating rpc client");
    status = gracht_client_create(&clientConfig, &__crt_gclient);
    if (status) {
        ERROR("[InitializeProcess] gracht_link_vali_client_create failed %i", status);
        _Exit(status);
    }
    
    // Get startup information
    TRACE("[InitializeProcess] receiving startup configuration");
    if (IsModule) {
        Syscall_ModuleGetStartupInfo(StartupInformation, &__crt_process_id, &__crt_raw_cmdline[0],
                                     sizeof(__crt_raw_cmdline));
    }
    else {
        UUId_t dmaHandle = tls_current()->transfer_buffer.handle;
        char*  dmaBuffer = tls_current()->transfer_buffer.buffer;
        size_t maxLength = tls_current()->transfer_buffer.length;

        svc_process_get_startup_information(GetGrachtClient(), &msg.base, thrd_current(), dmaHandle, maxLength);
        gracht_client_wait_message(GetGrachtClient(), &msg.base, GetGrachtBuffer(), GRACHT_WAIT_BLOCK);
        svc_process_get_startup_information_result(GetGrachtClient(), &msg.base,
                                                   &osStatus, &__crt_process_id, &StartupInformation->ArgumentsLength,
                                                   &StartupInformation->InheritationLength, &StartupInformation->LibraryEntriesLength);
        
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
        memcpy(&__crt_raw_cmdline[0], StartupInformation->Arguments, StartupInformation->ArgumentsLength);
    }

    // Parse the configuration information
    StdioConfigureStandardHandles(StartupInformation->Inheritation);
}

int IsProcessModule(void)
{
    return __crt_is_module;
}

UUId_t* GetInternalProcessId(void)
{
    return &__crt_process_id;
}

const char* GetInternalCommandLine(void)
{
    return &__crt_raw_cmdline[0];
}

gracht_client_t* GetGrachtClient(void)
{
    return __crt_gclient;
}

void* GetGrachtBuffer(void)
{
    return (void*)&__crt_gclient_buffer[0];
}

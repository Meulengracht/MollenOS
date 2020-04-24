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

#include <internal/_ipc.h>
#include <internal/_syscalls.h>
#include <internal/_utils.h>
#include <threads.h>

extern void StdioInitialize(void *InheritanceBlock);
extern void StdSignalInitialize(void);

static char             __CrtStartupBuffer[1024] = { 0 };
static gracht_client_t* __CrtClient              = NULL;
static int              __CrtIsModule            = 0;
static UUId_t           __CrtProcessId           = UUID_INVALID;

void InitializeProcess(int IsModule, ProcessStartupInformation_t* StartupInformation)
{
    gracht_client_configuration_t clientConfig;
    struct vali_link_message      msg = VALI_MSG_INIT_HANDLE(GetProcessService());
    OsStatus_t                    status;
    
    // We must set IsModule before anything
    __CrtIsModule = IsModule;

    // Create the ipc client
    gracht_link_vali_client_create(&clientConfig.link);
    gracht_client_create(&clientConfig, &__CrtClient);

    // Get startup information
    if (IsModule) {
        Syscall_ModuleGetStartupInfo(StartupInformation, &__CrtProcessId, &__CrtStartupBuffer[0],
            sizeof(__CrtStartupBuffer));
    }
    else {
        svc_process_get_startup_information(GetGrachtClient(), &msg, thrd_current(),
            &status, &__CrtProcessId, &StartupInformation->ArgumentsLength,
            &StartupInformation->InheritationLength, &StartupInformation->LibraryEntriesLength,
            &__CrtStartupBuffer[0], sizeof(__CrtStartupBuffer));
        gracht_vali_message_finish(&msg);
        
        // fixup pointers
        StartupInformation->Arguments      = &__CrtStartupBuffer[0];
        StartupInformation->Inheritation   = &__CrtStartupBuffer[StartupInformation->ArgumentsLength];
        StartupInformation->LibraryEntries = &__CrtStartupBuffer[StartupInformation->ArgumentsLength + StartupInformation->InheritationLength];
    }
    
	// Initialize STD-C
	StdioInitialize(StartupInformation->Inheritation);
    StdSignalInitialize();
}

int IsProcessModule(void)
{
    return __CrtIsModule;
}

UUId_t* GetInternalProcessId(void)
{
    return &__CrtProcessId;
}

const char* GetInternalCommandLine(void)
{
    return &__CrtStartupBuffer[0];
}

gracht_client_t* GetGrachtClient(void)
{
    return __CrtClient;
}

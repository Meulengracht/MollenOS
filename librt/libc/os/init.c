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

#include <ddk/services/process.h>
#include <gracht/link/vali.h>
#include <internal/_syscalls.h>
#include <internal/_utils.h>
#include <os/services/process.h>

extern void StdioInitialize(void *InheritanceBlock, size_t InheritanceBlockLength);
extern void StdSignalInitialize(void);

static char             __CrtArgumentBuffer[512]    = { 0 };
static char             __CrtInheritanceBuffer[512] = { 0 };
static gracht_client_t* __CrtClient                 = NULL;
static int              __CrtIsModule               = 0;
static UUId_t           __CrtProcessId              = UUID_INVALID;

void InitializeProcess(int IsModule, ProcessStartupInformation_t* StartupInformation)
{
    gracht_client_configuration_t clientConfig;
    size_t                        inheritanceBlockLength = sizeof(__CrtInheritanceBuffer);
    size_t                        argumentBlockLength    = sizeof(__CrtArgumentBuffer);
    
    _CRT_UNUSED(StartupInformation);

    // We must set IsModule before anything
    __CrtIsModule  = IsModule;
    __CrtProcessId = ProcessGetCurrentId();

    // Create the ipc client
    gracht_link_vali_client_create(&clientConfig.link);
    gracht_client_create(&clientConfig, &__CrtClient);

    // Get startup information
    if (IsModule) {
        Syscall_ModuleGetStartupInfo(&__CrtInheritanceBuffer[0], &inheritanceBlockLength, 
                                     &__CrtArgumentBuffer[0], &argumentBlockLength);
    }
    else {
        GetProcessInheritationBlock(&__CrtInheritanceBuffer[0], &inheritanceBlockLength);
        GetProcessCommandLine(&__CrtArgumentBuffer[0], &argumentBlockLength);
    }
    
	// Initialize STD-C
	StdioInitialize((void*)&__CrtInheritanceBuffer[0], inheritanceBlockLength);
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
    return &__CrtArgumentBuffer[0];
}

gracht_client_t* GetGrachtClient(void)
{
    return __CrtClient;
}

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

#include <internal/_syscalls.h>
#include <internal/_utils.h>
#include <os/services/process.h>
#include <ddk/services/process.h>

extern void StdioInitialize(void *InheritanceBlock, size_t InheritanceBlockLength);
extern void StdSignalInitialize(void);

static char   __CrtArgumentBuffer[512]    = { 0 };
static char   __CrtInheritanceBuffer[512] = { 0 };
static int    __CrtIsModule               = 0;
static UUId_t __CrtProcessId              = UUID_INVALID;

void InitializeProcess(int IsModule, ProcessStartupInformation_t* StartupInformation)
{
    size_t InheritanceBlockLength = sizeof(__CrtInheritanceBuffer);
    size_t ArgumentBlockLength    = sizeof(__CrtArgumentBuffer);
    _CRT_UNUSED(StartupInformation);

    // We must set IsModule before anything
    __CrtIsModule  = IsModule;
    __CrtProcessId = ProcessGetCurrentId();

    // Get startup information
    if (IsModule) {
        Syscall_ModuleGetStartupInfo(&__CrtInheritanceBuffer[0], &InheritanceBlockLength, 
                                     &__CrtArgumentBuffer[0], &ArgumentBlockLength);
    }
    else {
        GetProcessInheritationBlock(&__CrtInheritanceBuffer[0], &InheritanceBlockLength);
        GetProcessCommandLine(&__CrtArgumentBuffer[0], &ArgumentBlockLength);
    }
    
	// Initialize STD-C
	StdioInitialize((void*)&__CrtInheritanceBuffer[0], InheritanceBlockLength);
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

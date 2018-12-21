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
 * MollenOS C-Support Initialize Implementation
 * - Definitions, prototypes and information needed.
 */

#include <internal/_syscalls.h>
#include <os/process.h>

extern void StdioInitialize(void *InheritanceBlock, size_t InheritanceBlockLength);
extern void StdSignalInitialize(void);

static char __CrtArgumentBuffer[512]    = { 0 };
static char __CrtInheritanceBuffer[512] = { 0 };
static int  __CrtIsModule               = 0;

void InitializeProcess(int IsModule, ProcessStartupInformation_t* StartupInformation)
{
    // Store the module information for later
    __CrtIsModule = IsModule;

    // Get startup information
    StartupInformation->ArgumentPointer         = &__CrtArgumentBuffer[0];
    StartupInformation->ArgumentLength          = sizeof(__CrtArgumentBuffer);
    StartupInformation->InheritanceBlockPointer = &__CrtInheritanceBuffer[0];
    StartupInformation->InheritanceBlockLength  = sizeof(__CrtInheritanceBuffer);
    if (IsModule) {
        Syscall_ModuleGetStartupInfo(StartupInformation);
    }
    else {
        ProcessGetStartupInformation(StartupInformation);
    }

	// Initialize STD-C
	StdioInitialize((void*)StartupInformation->InheritanceBlockPointer, StartupInformation->InheritanceBlockLength);
    StdSignalInitialize();
}

int IsProcessModule(void)
{
    return __CrtIsModule;
}

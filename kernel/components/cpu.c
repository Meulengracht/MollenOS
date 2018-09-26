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
 * MollenOS System Component Infrastructure 
 * - The Central Processing Unit Component is one of the primary actors
 *   in a domain. 
 */
#define __MODULE "CCPU"
#define __TRACE

#include <component/domain.h>
#include <component/cpu.h>
#include <system/interrupts.h>
#include <system/utils.h>
#include <machine.h>
#include <assert.h>
#include <string.h>
#include <debug.h>
#include <heap.h>

/* Static per-cpu data
 * We do not need to support more than 256 cpus because of APIC id's on the x86 arch. */
static SystemCpuCore_t* CpuStorageTable[256] = { 0 };

/* RegisterStaticCore
 * Registers the primary core for the given cpu. The core count and the
 * application-core will be initialized on first call to this function. This also allocates a 
 * new instance of the cpu-core. */
void
RegisterStaticCore(
    _In_ SystemCpuCore_t*   Core)
{
    // Register in lookup table
    assert(Core->Id < 256);
    CpuStorageTable[Core->Id] = Core;
}

/* RegisterApplicationCore
 * Registers a new cpu application core for the given cpu. The core count and the
 * application-core will be initialized on first call to this function. This also allocates a 
 * new instance of the cpu-core. */
void
RegisterApplicationCore(
    _In_ SystemCpu_t*       Cpu,
    _In_ UUId_t             CoreId,
    _In_ SystemCpuState_t   InitialState,
    _In_ int                External)
{
    int i;

    // Sanitize params
    assert(Cpu != NULL);
    assert(Cpu->NumberOfCores > 1);

    // Make sure the array is allocated
    if(Cpu->ApplicationCores == NULL) {
        if (Cpu->NumberOfCores > 1) {
            Cpu->ApplicationCores   = (SystemCpuCore_t*)kmalloc(sizeof(SystemCpuCore_t) * (Cpu->NumberOfCores - 1));
            memset((void*)Cpu->ApplicationCores, 0, sizeof(SystemCpuCore_t) * (Cpu->NumberOfCores - 1));
            for (i = 0; i < (Cpu->NumberOfCores - 1); i++) {
                Cpu->ApplicationCores[i].Id     = UUID_INVALID;
                Cpu->ApplicationCores[i].State  = CpuStateUnavailable;
            }
        }
    }

    // Find it and update parameters for it
    for (i = 0; i < (Cpu->NumberOfCores - 1); i++) {
        if (Cpu->ApplicationCores[i].Id == UUID_INVALID) {
            Cpu->ApplicationCores[i].Id         = CoreId;
            Cpu->ApplicationCores[i].State      = InitialState;
            Cpu->ApplicationCores[i].External   = External;
            CpuStorageTable[CoreId]             = &Cpu->ApplicationCores[i];
            break;
        }
    }
}

/* ActivateApplicationCore 
 * Activates the given core and prepares it for usage. This sets up a new 
 * scheduler and initializes a new idle thread. This function does never return. */
void
ActivateApplicationCore(
    _In_ SystemCpuCore_t*   Core)
{
    SystemDomain_t *Domain;
    OsStatus_t Status;

    // Notify everyone that we are running
    GetMachine()->NumberOfActiveCores++;
    Core->State = CpuStateRunning;

    // Create the idle-thread and scheduler for the core
	Status = ThreadingEnable();
    if (Status != OsSuccess) {
        ERROR("Failed to enable threading for application core %u.", Core->Id);
        CpuIdle();
    }
    InterruptEnable();

    // Bootup rest of cores in this domain if we are the primary core of
    // this domain. Then our job is simple
    Domain = GetCurrentDomain();
    if (Domain != NULL && GetCurrentProcessorCore() == &Domain->CoreGroup.PrimaryCore) {
        for (int i = 0; i < (Domain->CoreGroup.NumberOfCores - 1); i++) {
            StartApplicationCore(&Domain->CoreGroup.ApplicationCores[i]);
        }
    }

    // Enter idle loop
    WARNING("Core %u is online", Core->Id);
	while (1) {
		CpuIdle();
    }
}

/* EnableMultiProcessoringMode
 * If multiple cores are present this will boot them up. If multiple domains are present
 * it will boot all primary cores in each domain, then boot up rest of cores in our own domain. */
void
EnableMultiProcessoringMode(void)
{
    SystemDomain_t *CurrentDomain = GetCurrentDomain();
    SystemDomain_t *Domain;
    int i;

    // Boot all cores in our own domain, then boot the initial core
    // for all the other domains, they will boot up their own domains.
    foreach (DomainNode, GetDomains()) {
        Domain = (SystemDomain_t*)DomainNode->Data;
        if (Domain != CurrentDomain) {
            StartApplicationCore(&Domain->CoreGroup.PrimaryCore);
        }
    }

    if (CurrentDomain != NULL) {
        // Don't ever include ourself
        for (i = 0; i < (CurrentDomain->CoreGroup.NumberOfCores - 1); i++) {
            StartApplicationCore(&CurrentDomain->CoreGroup.ApplicationCores[i]);
        }
    }
    else {
        // No domains in system - boot all cores except ourself
        for (i = 0; i < (GetMachine()->Processor.NumberOfCores - 1); i++) {
            StartApplicationCore(&GetMachine()->Processor.ApplicationCores[i]);
        }
    }
}

/* GetProcessorCore
 * Retrieves the cpu core from the given core-id. */
SystemCpuCore_t*
GetProcessorCore(
    _In_ UUId_t             CoreId)
{
    assert(CpuStorageTable[CoreId] != NULL);
    return CpuStorageTable[CoreId];
}

/* GetCurrentProcessorCore
 * Retrieves the cpu core that belongs to calling cpu core. */
SystemCpuCore_t*
GetCurrentProcessorCore(void)
{
    assert(CpuStorageTable[CpuGetCurrentId()] != NULL);
    return CpuStorageTable[CpuGetCurrentId()];
}

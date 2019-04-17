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
#include <arch/interrupts.h>
#include <arch/utils.h>
#include <machine.h>
#include <assert.h>
#include <string.h>
#include <debug.h>
#include <heap.h>

/* Static per-cpu data
 * We do not need to support more than 256 cpus because of APIC id's on the x86 arch. 
 * How about on the x2apic? */
static SystemCpuCore_t* CpuStorageTable[256] = { 0 };

SystemCpuCore_t*
GetProcessorCore(
    _In_ UUId_t CoreId)
{
    assert(CpuStorageTable[CoreId] != NULL);
    return CpuStorageTable[CoreId];
}

SystemCpuCore_t*
GetCurrentProcessorCore(void)
{
    assert(CpuStorageTable[ArchGetProcessorCoreId()] != NULL);
    return CpuStorageTable[ArchGetProcessorCoreId()];
}

void
RegisterStaticCore(
    _In_ SystemCpuCore_t* Core)
{
    // Register in lookup table
    assert(Core->Id < 256);
    CpuStorageTable[Core->Id] = Core;
}

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

    // Make sure the array is allocated and initialized
    if(Cpu->ApplicationCores == NULL) {
        if (Cpu->NumberOfCores > 1) {
            Cpu->ApplicationCores = (SystemCpuCore_t*)kmalloc(sizeof(SystemCpuCore_t) * (Cpu->NumberOfCores - 1));
            memset((void*)Cpu->ApplicationCores, 0, sizeof(SystemCpuCore_t) * (Cpu->NumberOfCores - 1));
            
            for (i = 0; i < (Cpu->NumberOfCores - 1); i++) {
                Cpu->ApplicationCores[i].Id     = UUID_INVALID;
                Cpu->ApplicationCores[i].State  = CpuStateUnavailable;
                SpinlockReset(&Cpu->ApplicationCores[i].FunctionState.SyncObject);
            }
        }
    }

    // Find it and update parameters for it
    for (i = 0; i < (Cpu->NumberOfCores - 1); i++) {
        if (Cpu->ApplicationCores[i].Id == UUID_INVALID) {
            Cpu->ApplicationCores[i].Id       = CoreId;
            Cpu->ApplicationCores[i].State    = InitialState;
            Cpu->ApplicationCores[i].External = External;
            CpuStorageTable[CoreId]           = &Cpu->ApplicationCores[i];
            break;
        }
    }
}

void
ActivateApplicationCore(
    _In_ SystemCpuCore_t* Core)
{
    SystemDomain_t* Domain;
    OsStatus_t      Status;

    // Create the idle-thread and scheduler for the core
	Status = ThreadingEnable();
    if (Status != OsSuccess) {
        ERROR("Failed to enable threading for application core %" PRIuIN ".", Core->Id);
        ArchProcessorIdle();
    }
    
    // Notify everyone that we are running beore switching on interrupts
    GetMachine()->NumberOfActiveCores++;
    Core->State = CpuStateRunning;
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
    WARNING("Core %" PRIuIN " is online", Core->Id);
	while (1) {
		ArchProcessorIdle();
    }
}

int
ExecuteProcessorFunction(
    _In_     int                     ExcludeSelf,
    _In_     SystemCpuFunctionType_t Type,
    _In_Opt_ SystemCpuFunction_t     Function,
    _In_Opt_ void*                   Argument)
{
    SystemDomain_t*  Domain;
    SystemCpu_t*     Processor;
    SystemCpuCore_t* CurrentCore = GetCurrentProcessorCore();
    int              Executions = 0;
    
    Domain = GetCurrentDomain();
    if (Domain != NULL) {
        Processor = &Domain->CoreGroup;
    }
    else {
        Processor = &GetMachine()->Processor;
    }
    
    if (!ExcludeSelf || (ExcludeSelf && Processor->PrimaryCore.Id != CurrentCore->Id)) {
        if (Processor->PrimaryCore.State == CpuStateRunning) {
            ExecuteProcessorCoreFunction(Processor->PrimaryCore.Id, Type, Function, Argument);
            Executions++;
        }
    }
    
    for (int i = 0; i < (Processor->NumberOfCores - 1); i++) {
        if (!ExcludeSelf || (ExcludeSelf && Processor->ApplicationCores[i].Id != CurrentCore->Id)) {
            if (Processor->ApplicationCores[i].State == CpuStateRunning) {
                ExecuteProcessorCoreFunction(Processor->ApplicationCores[i].Id, Type, Function, Argument);
                Executions++;
            }
        }
    }
    return Executions;
}

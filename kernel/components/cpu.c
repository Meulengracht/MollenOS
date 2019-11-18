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
 * System Component Infrastructure 
 * - The Central Processing Unit Component is one of the primary actors
 *   in a domain. 
 */
#define __MODULE "CCPU"
#define __TRACE

#include <component/domain.h>
#include <component/cpu.h>
#include <arch/interrupts.h>
#include <arch/thread.h>
#include <arch/utils.h>
#include <machine.h>
#include <assert.h>
#include <string.h>
#include <debug.h>
#include <heap.h>

// 256 is a temporary number, once we start getting processors with more than
// 256 TXU's then we are fucked
static SystemCpuCore_t* TxuTable[256]   = { 0 };
static MemoryCache_t*   TxuMessageCache = NULL;

SystemCpuCore_t*
GetProcessorCore(
    _In_ UUId_t CoreId)
{
    assert(TxuTable[CoreId] != NULL);
    return TxuTable[CoreId];
}

SystemCpuCore_t*
GetCurrentProcessorCore(void)
{
    assert(TxuTable[ArchGetProcessorCoreId()] != NULL);
    return TxuTable[ArchGetProcessorCoreId()];
}

static void
RegisterStaticCore(
    _In_ SystemCpuCore_t* Core)
{
    assert(Core->Id < 256);
    TxuTable[Core->Id] = Core;
}

void
InitializeProcessor(
    _In_ SystemCpu_t* Cpu)
{
    ArchProcessorInitialize(Cpu);
    RegisterStaticCore(&Cpu->PrimaryCore);
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
            }
        }
    }

    // Find it and update parameters for it
    for (i = 0; i < (Cpu->NumberOfCores - 1); i++) {
        if (Cpu->ApplicationCores[i].Id == UUID_INVALID) {
            Cpu->ApplicationCores[i].Id       = CoreId;
            Cpu->ApplicationCores[i].State    = InitialState;
            Cpu->ApplicationCores[i].External = External;
            TxuTable[CoreId]                  = &Cpu->ApplicationCores[i];
            break;
        }
    }
}

void
ActivateApplicationCore(
    _In_ SystemCpuCore_t* Core)
{
    SystemDomain_t* Domain;

    // Create the idle-thread and scheduler for the core
    ThreadingEnable();
    
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

void
TxuMessageCacheInitialize(void)
{
    TxuMessageCache = MemoryCacheCreate("txu_message_cache", 
        sizeof(TxuMessage_t), 0, 0 /* HEAP_SLAB_NO_ATOMIC_CACHE */, 
        NULL, NULL);
    assert(TxuMessageCache != NULL);
}

TxuMessage_t*
TxuMessageAllocate(void)
{
    return MemoryCacheAllocate(TxuMessageCache);
}

void
TxuMessageFree(
    _In_ TxuMessage_t* Message)
{
    return MemoryCacheFree(TxuMessageCache, Message);
}

int
ProcessorMessageSend(
    _In_ int                     ExcludeSelf,
    _In_ SystemCpuFunctionType_t Type,
    _In_ TxuMessage_t*           Message)
{
    SystemDomain_t*  Domain;
    SystemCpu_t*     Processor;
    SystemCpuCore_t* CurrentCore = GetCurrentProcessorCore();
    int              Executions = 0;
    
    assert(Message != NULL);
    
    Domain = GetCurrentDomain();
    if (Domain != NULL) {
        Processor = &Domain->CoreGroup;
    }
    else {
        Processor = &GetMachine()->Processor;
    }
    
    if (!ExcludeSelf || (ExcludeSelf && Processor->PrimaryCore.Id != CurrentCore->Id)) {
        if (Processor->PrimaryCore.State == CpuStateRunning) {
            TxuMessageSend(Processor->PrimaryCore.Id, Type, Message);
            Executions++;
        }
    }
    
    for (int i = 0; i < (Processor->NumberOfCores - 1); i++) {
        if (!ExcludeSelf || (ExcludeSelf && Processor->ApplicationCores[i].Id != CurrentCore->Id)) {
            if (Processor->ApplicationCores[i].State == CpuStateRunning) {
                TxuMessageSend(Processor->ApplicationCores[i].Id, Type, Message);
                Executions++;
            }
        }
    }
    return Executions;
}

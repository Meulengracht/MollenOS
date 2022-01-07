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
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 * System Component Infrastructure 
 * - The Central Processing Unit Component is one of the primary actors
 *   in a domain. 
 */
#define __MODULE "CCPU"
#define __TRACE

#include <arch/interrupts.h>
#include <arch/utils.h>
#include <assert.h>
#include <component/domain.h>
#include <component/cpu.h>
#include <ddk/io.h>
#include <debug.h>
#include <heap.h>
#include <machine.h>
#include <string.h>

#include "cpu_private.h"

// 256 is a temporary number, once we start getting processors with more than
// 256 TXU's then we are fucked
static SystemCpuCore_t* g_coreTable[256] = { 0 };

SystemCpuCore_t*
GetProcessorCore(
    _In_ UUId_t CoreId)
{
    return g_coreTable[CoreId];
}

SystemCpuCore_t*
CpuCoreCurrent(void)
{
    return g_coreTable[ArchGetProcessorCoreId()];
}

static void
__ConstructCpuCore(
        _In_ SystemCpuCore_t* core,
        _In_ UUId_t           coreId,
        _In_ SystemCpuState_t state,
        _In_ int              external)
{
    // zero out the entire structure first
    memset(core, 0, sizeof(SystemCpuCore_t));

    core->Id       = coreId;
    core->State    = state;
    core->External = external;

    // Scheduler  = { 0 } TODO SchedulerConstruct
    queue_construct(&core->FunctionQueue[0]);
    queue_construct(&core->FunctionQueue[1]);
}

void
CpuInitializePlatform(
        _In_ SystemCpu_t*     cpu,
        _In_ SystemCpuCore_t* core)
{
    // Initialize the boot cpu's primary core pointer immediately before
    // passing it down to the arch layer - so it can use the storage.
    cpu->NumberOfCores = 1;
    cpu->Cores         = core;

    __ConstructCpuCore(core, UUID_INVALID, CpuStateRunning, 0);
    ArchPlatformInitialize(cpu, core);
    
    // Register the primary core that has been registered
    g_coreTable[cpu->Cores->Id] = cpu->Cores;
}

void
RegisterApplicationCore(
    _In_ SystemCpu_t*     cpu,
    _In_ UUId_t           coreId,
    _In_ SystemCpuState_t initialState,
    _In_ int              external)
{
    SystemCpuCore_t* core;
    SystemCpuCore_t* i;

    assert(cpu != NULL);
    assert(cpu->NumberOfCores > 1);

    core = (SystemCpuCore_t*)kmalloc(sizeof(SystemCpuCore_t));
    assert(core != NULL);
    __ConstructCpuCore(core, coreId, initialState, external);

    // Add the core to the list of cores in this cpu
    i = cpu->Cores;
    while (i->Link) {
        i = i->Link;
    }
    i->Link = core;
    
    // Register the TXU in the table for quick access
    g_coreTable[coreId] = core;
}

void
ActivateApplicationCore(
    _In_ SystemCpuCore_t* Core)
{
    SystemDomain_t*  Domain;
    SystemCpuCore_t* Iter;
    TRACE("[activate_core] %u", Core->Id);

    // Create the idle-thread and scheduler for the core
    ThreadingEnable();
    
    // Notify everyone that we are running beore switching on interrupts, don't
    // add this flag, overwrite and set only this flag
    WRITE_VOLATILE(Core->State, CpuStateRunning);
    atomic_fetch_add(&GetMachine()->NumberOfActiveCores, 1);
    InterruptEnable();

    // Bootup rest of cores in this domain if we are the primary core of
    // this domain. Then our job is simple
    Domain = GetCurrentDomain();
    if (Domain != NULL && Core == Domain->CoreGroup.Cores) {
        Iter = Domain->CoreGroup.Cores->Link;
        while (Iter) {
            StartApplicationCore(Iter);
            Iter = Iter->Link;
        }
    }

    // Enter idle loop
    WARNING("[activate_core] %" PRIuIN " is online", Core->Id);
	while (1) {
		ArchProcessorIdle();
    }
}

int
ProcessorMessageSend(
    _In_ int                     ExcludeSelf,
    _In_ SystemCpuFunctionType_t Type,
    _In_ TxuFunction_t           Function,
    _In_ void*                   Argument,
    _In_ int                     Asynchronous)
{
    SystemDomain_t*  Domain;
    SystemCpu_t*     Processor;
    SystemCpuCore_t* CurrentCore = CpuCoreCurrent();
    SystemCpuCore_t* Iter;
    OsStatus_t       Status;
    int              Executions = 0;
    
    assert(Function != NULL);
    
    Domain = GetCurrentDomain();
    if (Domain != NULL) {
        Processor = &Domain->CoreGroup;
    }
    else {
        Processor = &GetMachine()->Processor;
    }
    
    Iter = Processor->Cores;
    while (Iter) {
        if (ExcludeSelf && Iter == CurrentCore) {
            Iter = Iter->Link;
            continue;
        }
        
        if (READ_VOLATILE(Iter->State) & CpuStateRunning) {
            Status = TxuMessageSend(Iter->Id, Type, Function, Argument, Asynchronous);
            if (Status == OsSuccess) {
                Executions++;
            }
        }
        Iter = Iter->Link;
    }
    return Executions;
}

void
CpuCoreEnterInterrupt(
    _In_ Context_t* InterruptContext,
    _In_ int        InterruptPriority)
{
    SystemCpuCore_t* core = CpuCoreCurrent();
    if (!core->InterruptNesting) {
        InterruptSetActiveStatus(1);
        core->InterruptRegisters = InterruptContext;
        core->InterruptPriority  = InterruptPriority;
    }
    core->InterruptNesting++;
#ifdef __OSCONFIG_NESTED_INTERRUPTS
    InterruptEnable();
#endif
}

Context_t*
CpuCoreExitInterrupt(
    _In_ Context_t* InterruptContext,
    _In_ int        InterruptPriority)
{
    SystemCpuCore_t* core = CpuCoreCurrent();

#ifdef __OSCONFIG_NESTED_INTERRUPTS
    InterruptDisable();
#endif
    core->InterruptNesting--;
    if (!core->InterruptNesting) {
        InterruptContext = core->InterruptRegisters;
        core->InterruptRegisters = NULL;
        InterruptsSetPriority(core->InterruptPriority);
        InterruptSetActiveStatus(0);
    }
    else {
        InterruptsSetPriority(InterruptPriority);
    }
    return InterruptContext;
}


element_t*
CpuCorePopQueuedIpc(
        _In_ SystemCpuCore_t* CpuCore)
{
    if (!CpuCore) {
        return NULL;
    }
    return queue_pop(&CpuCore->FunctionQueue[CpuFunctionCustom]);
}

void
CpuCoreQueueIpc(
        _In_ SystemCpuCore_t*        CpuCore,
        _In_ SystemCpuFunctionType_t IpcType,
        _In_ element_t*              Element)
{
    if (!CpuCore) {
        return;
    }
    queue_push(&CpuCore->FunctionQueue[IpcType], Element);
}

UUId_t
CpuCoreId(
        _In_ SystemCpuCore_t* CpuCore)
{
    if (!CpuCore) {
        return UUID_INVALID;
    }
    return CpuCore->Id;
}

void
CpuCoreSetState(
        _In_ SystemCpuCore_t* CpuCore,
        _In_ SystemCpuState_t State)
{
    if (!CpuCore) {
        return;
    }
    WRITE_VOLATILE(CpuCore->State, State);
}

SystemCpuState_t
CpuCoreState(
        _In_ SystemCpuCore_t* CpuCore)
{
    if (!CpuCore) {
        return CpuStateUnavailable;
    }
    return READ_VOLATILE(CpuCore->State);
}

SystemCpuCore_t*
CpuCoreNext(
        _In_ SystemCpuCore_t* CpuCore)
{
    if (!CpuCore) {
        return NULL;
    }
    return CpuCore->Link;
}

void
CpuCoreSetPriority(
        _In_ SystemCpuCore_t* CpuCore,
        _In_ int              Priority)
{
    if (!CpuCore) {
        return;
    }
    CpuCore->InterruptPriority = Priority;
}

int
CpuCorePriority(
        _In_ SystemCpuCore_t* CpuCore)
{
    if (!CpuCore) {
        return 0;
    }
    return CpuCore->InterruptPriority;
}

void
CpuCoreSetCurrentThread(
        _In_ SystemCpuCore_t* CpuCore,
        _In_ Thread_t*        Thread)
{
    if (!CpuCore) {
        return;
    }
    CpuCore->CurrentThread = Thread;
}

Thread_t*
CpuCoreCurrentThread(
        _In_ SystemCpuCore_t* CpuCore)
{
    if (!CpuCore) {
        return NULL;
    }
    return CpuCore->CurrentThread;
}

Thread_t*
CpuCoreIdleThread(
        _In_ SystemCpuCore_t* CpuCore)
{
    if (!CpuCore) {
        return NULL;
    }
    return &CpuCore->IdleThread;
}

Scheduler_t*
CpuCoreScheduler(
        _In_ SystemCpuCore_t* CpuCore)
{
    if (!CpuCore) {
        return NULL;
    }
    return &CpuCore->Scheduler;
}

void
CpuCoreSetInterruptContext(
        _In_ SystemCpuCore_t* CpuCore,
        _In_ Context_t*       Context)
{
    if (!CpuCore) {
        return;
    }
    CpuCore->InterruptRegisters = Context;
}

Context_t*
CpuCoreInterruptContext(
        _In_ SystemCpuCore_t* CpuCore)
{
    if (!CpuCore) {
        return NULL;
    }
    return CpuCore->InterruptRegisters;
}

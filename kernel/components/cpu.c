/**
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
#include <handle.h>
#include <heap.h>
#include <machine.h>
#include <memoryspace.h>
#include <string.h>

#include "cpu_private.h"

// So yes, we could remove this and use the list and the TLS area. The reason we still
// keep this is to provide a quick lookup of core structures.
static SystemCpuCore_t* g_coreTable[__CPU_MAX_COUNT] = { 0 };

SystemCpuCore_t*
GetProcessorCore(
        _In_ uuid_t coreId)
{
    assert(coreId < __CPU_MAX_COUNT);
    return g_coreTable[coreId];
}

SystemCpuCore_t*
CpuCoreCurrent(void)
{
    return g_coreTable[ArchGetProcessorCoreId()];
}

static void
__ConstructCpuCore(
        _In_ SystemCpuCore_t* core,
        _In_ uuid_t           coreId,
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
CpuCoreRegister(
        _In_ SystemCpu_t*     cpu,
        _In_ uuid_t           coreId,
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

static void
__InitializeTLS(
        _In_ SystemCpuCore_t* cpuCore)
{
    // Set the first entry of the TLS to point to the core structure,
    // when threads migrate they need this pointer updated.
    __set_reserved(0, (uintptr_t)cpuCore);

    // add any other per-core data here
}

void
CpuCoreStart(void)
{
    SystemDomain_t*  domain;
    SystemCpuCore_t* i;
    SystemCpuCore_t* cpuCore;
    oscode_t       osStatus;
    uuid_t           memorySpace;

    TRACE("CpuCoreStart(core=%u)", ArchGetProcessorCoreId());

    // Retrieve the current core structure
    cpuCore = CpuCoreCurrent();
    if (!cpuCore) {
        ERROR("CpuCoreStart coreId=%u do not match one of the registered cores", ArchGetProcessorCoreId());
        ArchProcessorHalt();
    }

    // Now we need to do a new memory space for this core. We simply create a new memory space
    // with kernel flags and switch to it.
    osStatus = CreateMemorySpace(MEMORY_SPACE_INHERIT, &memorySpace);
    if (osStatus != OsOK) {
        // failed to boot this core!
        ERROR("CpuCoreStart failed to create idle memoryspace for coreId=%u", ArchGetProcessorCoreId());
        ArchProcessorHalt();
    }
    MemorySpaceSwitch(MEMORYSPACE_GET(memorySpace));

    // Now that we have a proper memoryspace for this idle thread, we can install TLS
    __InitializeTLS(cpuCore);

    // Create the idle-thread and scheduler for the core
    ThreadingEnable(cpuCore);
    
    // Notify everyone that we are running beore switching on interrupts, don't
    // add this flag, overwrite and set only this flag
    WRITE_VOLATILE(cpuCore->State, CpuStateRunning);
    atomic_fetch_add(&GetMachine()->NumberOfActiveCores, 1);
    InterruptEnable();

    // Bootup rest of cores in this domain if we are the primary core of
    // this domain. Then our job is simple
    domain = GetCurrentDomain();
    if (domain != NULL && cpuCore == domain->CoreGroup.Cores) {
        i = domain->CoreGroup.Cores->Link;
        while (i) {
            StartApplicationCore(i);
            i = i->Link;
        }
    }

    // Enter idle loop
    WARNING("CpuCoreStart %" PRIuIN " is online", cpuCore->Id);
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
    oscode_t       Status;
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
            if (Status == OsOK) {
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
        _In_ SystemCpuCore_t* cpuCore)
{
    if (!cpuCore) {
        return NULL;
    }
    return queue_pop(&cpuCore->FunctionQueue[CpuFunctionCustom]);
}

void
CpuCoreQueueIpc(
        _In_ SystemCpuCore_t*        cpuCore,
        _In_ SystemCpuFunctionType_t functionType,
        _In_ element_t*              element)
{
    if (!cpuCore) {
        return;
    }
    queue_push(&cpuCore->FunctionQueue[functionType], element);
}

uuid_t
CpuCoreId(
        _In_ SystemCpuCore_t* cpuCore)
{
    if (!cpuCore) {
        return UUID_INVALID;
    }
    return cpuCore->Id;
}

void
CpuCoreSetState(
        _In_ SystemCpuCore_t* cpuCore,
        _In_ SystemCpuState_t cpuState)
{
    if (!cpuCore) {
        return;
    }
    WRITE_VOLATILE(cpuCore->State, cpuState);
}

SystemCpuState_t
CpuCoreState(
        _In_ SystemCpuCore_t* cpuCore)
{
    if (!cpuCore) {
        return CpuStateUnavailable;
    }
    return READ_VOLATILE(cpuCore->State);
}

SystemCpuCore_t*
CpuCoreNext(
        _In_ SystemCpuCore_t* cpuCore)
{
    if (!cpuCore) {
        return NULL;
    }
    return cpuCore->Link;
}


PlatformCpuCoreBlock_t*
CpuCorePlatformBlock(
        _In_ SystemCpuCore_t* cpuCore)
{
    if (!cpuCore) {
        return NULL;
    }
    return &cpuCore->PlatformData;
}

void
CpuCoreSetPriority(
        _In_ SystemCpuCore_t* cpuCore,
        _In_ int              priority)
{
    if (!cpuCore) {
        return;
    }
    cpuCore->InterruptPriority = (uint32_t)priority;
}

int
CpuCorePriority(
        _In_ SystemCpuCore_t* cpuCore)
{
    if (!cpuCore) {
        return 0;
    }
    return (int)cpuCore->InterruptPriority;
}

void
CpuCoreSetCurrentThread(
        _In_ SystemCpuCore_t* cpuCore,
        _In_ Thread_t*        thread)
{
    if (!cpuCore) {
        return;
    }

    // If we are idle task - disable task priority
    if (ThreadFlags(thread) & THREADING_IDLE) {
        CpuCoreSetPriority(cpuCore, 0);
    }
    else {
        CpuCoreSetPriority(
                cpuCore,
                61 - SchedulerObjectGetQueue(
                        ThreadSchedulerHandle(thread)
                )
        );
    }
    cpuCore->CurrentThread = thread;
}

Thread_t*
CpuCoreCurrentThread(
        _In_ SystemCpuCore_t* cpuCore)
{
    if (!cpuCore) {
        return NULL;
    }
    return cpuCore->CurrentThread;
}

Thread_t*
CpuCoreIdleThread(
        _In_ SystemCpuCore_t* cpuCore)
{
    if (!cpuCore) {
        return NULL;
    }
    return &cpuCore->IdleThread;
}

Scheduler_t*
CpuCoreScheduler(
        _In_ SystemCpuCore_t* cpuCore)
{
    if (!cpuCore) {
        return NULL;
    }
    return &cpuCore->Scheduler;
}

void
CpuCoreSetInterruptContext(
        _In_ SystemCpuCore_t* cpuCore,
        _In_ Context_t*       context)
{
    if (!cpuCore) {
        return;
    }
    cpuCore->InterruptRegisters = context;
}

Context_t*
CpuCoreInterruptContext(
        _In_ SystemCpuCore_t* cpuCore)
{
    if (!cpuCore) {
        return NULL;
    }
    return cpuCore->InterruptRegisters;
}

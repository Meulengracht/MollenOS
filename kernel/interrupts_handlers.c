/**
 * MollenOS
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
 * Interrupt Interface
 * - Contains the shared kernel interrupt interface
 *   that is generic and can be shared/used by all systems
 */

#define __MODULE "irqs"
//#define __TRACE

#include <arch/utils.h>
#include <assert.h>
#include <component/cpu.h>
#include <debug.h>
#include <heap.h>
#include <interrupts.h>
#include <string.h>

static UUId_t        InterruptHandlers[CpuFunctionCount] = { 0 };
static MemoryCache_t IpiItemCache                        = { 0 };

InterruptStatus_t
ProcessorHaltHandler(
    _In_ FastInterruptResources_t* NotUsed,
    _In_ void*                     NotUsedEither)
{
    _CRT_UNUSED(NotUsed);
    _CRT_UNUSED(NotUsedEither);
    ArchProcessorHalt();
    return InterruptHandled;
}

InterruptStatus_t
FunctionExecutionInterruptHandler(
    _In_ FastInterruptResources_t* NotUsed,
    _In_ void*                     NotUsedEither)
{
    SystemCpuCore_t* Core = GetCurrentProcessorCore();
    element_t*       Node;
    TRACE("FunctionExecutionInterruptHandler(%u)", Core->Id);

    _CRT_UNUSED(NotUsed);
    _CRT_UNUSED(NotUsedEither);

    Node = queue_pop(&Core->FunctionQueue[CpuFunctionCustom]);
    while (Node != NULL) {
        SystemCoreFunctionItem_t* Item = Node->value;
        Item->Handler(Item->Argument);
        MemoryCacheFree(&IpiItemCache, Item);
        Node = queue_pop(&Core->FunctionQueue[CpuFunctionCustom]);
    }
    return InterruptHandled;
}

void
InitializeInterruptHandlers(void)
{
    DeviceInterrupt_t Interrupt = { { 0 } };
    int               i;

    // Initialize the ipi cache
    MemoryCacheConstruct(&IpiItemCache, "ipi_cache", 
        sizeof(SystemCoreFunctionItem_t), 0, 0, NULL, NULL);
        
    // Initialize the interrupt handlers array
    for (i = 0; i < CpuFunctionCount; i++) {
        InterruptHandlers[i] = UUID_INVALID;
    }

    // Install default software interrupts
    Interrupt.Vectors[0]            = INTERRUPT_NONE;
    Interrupt.Pin                   = INTERRUPT_NONE;
    Interrupt.Line                  = INTERRUPT_NONE;
    
    // Halt
    Interrupt.FastInterrupt.Handler    = ProcessorHaltHandler;
    InterruptHandlers[CpuFunctionHalt] = InterruptRegister(&Interrupt,
        INTERRUPT_SOFT | INTERRUPT_KERNEL | INTERRUPT_NOTSHARABLE);

    // Custom
    Interrupt.Line                       = INTERRUPT_NONE;
    Interrupt.FastInterrupt.Handler      = FunctionExecutionInterruptHandler;
    InterruptHandlers[CpuFunctionCustom] = InterruptRegister(&Interrupt,
        INTERRUPT_SOFT | INTERRUPT_KERNEL | INTERRUPT_NOTSHARABLE);
}

void
ExecuteProcessorCoreFunction(
    _In_     UUId_t                  CoreId,
    _In_     SystemCpuFunctionType_t Type,
    _In_Opt_ SystemCpuFunction_t     Function,
    _In_Opt_ void*                   Argument)
{
    // Get the function queue state object for the
    // target core, not the running one
    SystemCpuCore_t*          Core = GetProcessorCore(CoreId);
    SystemCoreFunctionItem_t* Item;
    assert(InterruptHandlers[Type] != UUID_INVALID);
    TRACE("[execute_irq] %u => %u, %u: 0x%x", 
        ArchGetProcessorCoreId(), CoreId, Type, InterruptHandlers[Type]);

    Item = (SystemCoreFunctionItem_t*)MemoryCacheAllocate(&IpiItemCache);
    if (!Item) {
        ERROR("[execute_irq] memory_cache_allocate returned NULL");
        assert(0);
    }
    
    memset(Item, 0, sizeof(SystemCoreFunctionItem_t));
    ELEMENT_INIT(&Item->Header, 0, Item);
    Item->Handler  = Function;
    Item->Argument = Argument;
    
    TRACE("[execute_irq] add node 0x%llx to 0x%llx", &Core->FunctionQueue[Type], &Item->Header);
    queue_push(&Core->FunctionQueue[Type], &Item->Header);
    ArchProcessorSendInterrupt(CoreId, InterruptHandlers[Type]);
}

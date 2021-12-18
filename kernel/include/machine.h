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
 * MollenOS Machine Structure
 * - Is the definition of what makes the currently running machine. From this
 *   structure all components in the system are accessible.
 */

#ifndef __VALI_MACHINE__
#define __VALI_MACHINE__

#include <ds/bounded_stack.h>
#include <os/osdefs.h>
#include <os/mollenos.h>
#include <irq_spinlock.h>
#include <vboot/vboot.h>
#include <time.h>
#include <utils/static_memory_pool.h>

// Components
#include <component/domain.h>
#include <component/memory.h>
#include <component/cpu.h>
#include <component/ic.h>

typedef struct SystemMachine {
    char         Architecture[32];
    char         Author[48];
    char         Date[32];
    unsigned     VersionMajor;
    unsigned     VersionMinor;
    unsigned     VersionRevision;
    struct VBoot BootInformation;

    // UMA Hardware Resources
    SystemCpu_t     Processor;      // Used in UMA mode
    MemorySpace_t   SystemSpace;    // Used in UMA mode
    bounded_stack_t PhysicalMemory;
    IrqSpinlock_t   PhysicalMemoryLock;
    
    // Global Hardware Resources
    StaticMemoryPool_t          GlobalAccessMemory;
    SystemMemoryMap_t           MemoryMap;
    list_t                      SystemDomains;
    SystemInterruptController_t* InterruptController;
    int                         NumberOfOverrides;
    SystemInterruptOverride_t*  Overrides;
    SystemTime_t                SystemTime;

    // Total information across domains
    _Atomic(int)                NumberOfProcessors;
    _Atomic(int)                NumberOfCores;
    _Atomic(int)                NumberOfActiveCores;
    size_t                      NumberOfMemoryBlocks;
    size_t                      MemoryGranularity;
} SystemMachine_t;

/**
 * @brief Initializes the kernel, this is expected to be the first function called upon
 * kernel entry.
 *
 * @param bootInformation [In] A pointer to the VBoot protocol structure.
 */
_Noreturn void
InitializeMachine(
        _In_ struct VBoot* bootInformation);

/**
 * GetMachine
 * Retrieves a pointer for the machine structure.
 */
KERNELAPI SystemMachine_t* KERNELABI
GetMachine(void);

/**
 * SetMachineUmaMode (@arch)
 * Sets the current machine into UMA mode which means there are no domains. This must
 * be implemented by the architecture to allow a arch-specific setup of the UMA topology.
 */
KERNELAPI void KERNELABI
SetMachineUmaMode(void);

/**
 * InitializeSystemTimers (@arch)
 * Register and start all neccessary system timers for the operating system to run.
 */
KERNELAPI OsStatus_t KERNELABI
InitializeSystemTimers(void);

/**
 * @brief Initializes the entire system memory range, selecting ranges that should
 * be reserved and those that are free for system use.
 *
 * @param machine [In] The machine to initialize memory systems for.
 * @return             Status of the initialization.
 */
KERNELAPI OsStatus_t KERNELABI
MachineInitializeMemorySystems(
        _In_ SystemMachine_t* machine);

/**
 *
 * @param size
 * @param memory
 * @return
 */
KERNELAPI OsStatus_t KERNELABI
MachineAllocateBootMemory(
        _In_  size_t size,
        _Out_ void** memory);

/**
 * @brief Tries to allocate the requested number of memory pages
 *
 * @param PageCount The number of physical memory pages to allocate
 * @return          The status of the operation
 */
KERNELAPI OsStatus_t KERNELABI
AllocatePhysicalMemory(
    _In_ int        PageCount,
    _In_ uintptr_t* Pages);
#endif // !__VALI_MACHINE__

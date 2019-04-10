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

#ifndef __COMPONENT_CPU__
#define __COMPONENT_CPU__

#include <os/osdefs.h>
#include <memoryspace.h>
#include <threading.h>
#include <scheduler.h>

typedef enum _SystemCpuState {
    CpuStateUnavailable     = 0x0,
    CpuStateShutdown        = 0x1,
    CpuStateRunning         = 0x2,

    CpuStateInterruptActive = 0x10000
} SystemCpuState_t;

typedef enum _SystemCpuInterruptReason {
    CpuInterruptHalt,
    CpuInterruptYield
} SystemCpuInterruptReason_t;

typedef struct _SystemCpuCore {
    UUId_t              Id;
    SystemCpuState_t    State;
    int                 External;

    // Static resources
    MCoreThread_t       IdleThread;
    SystemScheduler_t   Scheduler;

    // State resources
    MCoreThread_t*      CurrentThread;
} SystemCpuCore_t;

typedef struct _SystemCpu {
    char                Vendor[16];     // zero terminated string
    char                Brand[64];      // zero terminated string
    uintptr_t           Data[4];        // data available for usage
    int                 NumberOfCores;  // always minimum 1

    SystemCpuCore_t     PrimaryCore;
    SystemCpuCore_t*    ApplicationCores;
    
    struct _SystemCpu*  Link;
} SystemCpu_t;

/* EnableMultiProcessoringMode
 * If multiple cores are present this will boot them up. If multiple domains are present
 * it will boot all primary cores in each domain, then boot up rest of cores in our own domain. */
KERNELAPI void KERNELABI
EnableMultiProcessoringMode(void);

/* RegisterStaticCore
 * Registers the primary core for the given cpu. The core count and the
 * application-core will be initialized on first call to this function. This also allocates a 
 * new instance of the cpu-core. */
KERNELAPI void KERNELABI
RegisterStaticCore(
    _In_ SystemCpuCore_t*   Core);

/* RegisterApplicationCore
 * Registers a new cpu application core for the given cpu. The core count and the
 * application-core will be initialized on first call to this function. This also allocates a 
 * new instance of the cpu-core. */
KERNELAPI void KERNELABI
RegisterApplicationCore(
    _In_ SystemCpu_t*       Cpu,
    _In_ UUId_t             CoreId,
    _In_ SystemCpuState_t   InitialState,
    _In_ int                External);

/* ActivateApplicationCore 
 * Activates the given core and prepares it for usage. This sets up a new 
 * scheduler and initializes a new idle thread. This function does never return. */
KERNELAPI void KERNELABI
ActivateApplicationCore(
    _In_ SystemCpuCore_t*   Core);

/* GetProcessorCore
 * Retrieves the cpu core from the given core-id. */
KERNELAPI SystemCpuCore_t* KERNELABI
GetProcessorCore(
    _In_ UUId_t             CoreId);

/* GetCurrentProcessorCore
 * Retrieves the cpu core that belongs to calling cpu core. */
KERNELAPI SystemCpuCore_t* KERNELABI
GetCurrentProcessorCore(void);

/* InitializeProcessor (@arch)
 * Initializes the cpu as much as neccessary for the system to be in a running state. This
 * also initializes the primary core of the cpu structure. */
KERNELAPI void KERNELABI
InitializeProcessor(
    _In_ SystemCpu_t*       Cpu);

/* StartApplicationCore (@arch)
 * Initializes and starts the cpu core given. This is called by the kernel if it detects multiple
 * cores in the processor. */
KERNELAPI void KERNELABI
StartApplicationCore(
    _In_ SystemCpuCore_t*   Core);

/* InterruptProcessorCore (@arch) 
 * Interrupts the given core with a specified reason, how the reason is handled is 
 * implementation specific. */
KERNELAPI void KERNELABI
InterruptProcessorCore(
    _In_ UUId_t                     CoreId,
    _In_ SystemCpuInterruptReason_t Reason);

#endif // !__COMPONENT_CPU__

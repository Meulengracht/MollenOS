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
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 * System Component Infrastructure 
 * - The Central Processing Unit Component is one of the primary actors
 *   in a domain.
 * 
 * The processor object is the primary object and represents a physical
 * processor. A processor contains multiple core objects, and each core object
 * represents a physical core inside a processor. Each core object can contain
 * a number of TXU (Thread eXecution Units). The usual number would be one 
 * or two (HT). Each of these TXU's has the ability to schedule threads, handle
 * interrupts, send messages etc.
 * 
 */

#ifndef __COMPONENT_CPU__
#define __COMPONENT_CPU__

#include <arch/platform.h>
#include <ds/queue.h>
#include <memoryspace.h>
#include <threading.h>
#include <scheduler.h>

#define __CPU_MAX_COUNT 256

typedef void(*TxuFunction_t)(void*);
DECL_STRUCT(SystemCpuCore);

typedef enum SystemCpuState {
    CpuStateUnavailable     = 0x0U,
    CpuStateShutdown        = 0x1U,
    CpuStateRunning         = 0x2U,

    CpuStateInterruptActive = 0x10000U
} SystemCpuState_t;

typedef enum SystemCpuFunctionType {
    CpuFunctionHalt,
    CpuFunctionCustom,

    CpuFunctionCount
} SystemCpuFunctionType_t;

typedef struct TxuMessage {
    element_t     Header;
    _Atomic(int)  Delivered;
    TxuFunction_t Handler;
    void*         Argument;
} TxuMessage_t;

typedef struct SystemCpu {
    char               Vendor[16];     // zero terminated string
    char               Brand[64];      // zero terminated string
    int                NumberOfCores;  // always minimum 1
    SystemCpuCore_t*   Cores;
    struct SystemCpu*  Link;
    PlatformCpuBlock_t PlatformData;
} SystemCpu_t;

#define SYSTEM_CPU_INIT { { 0 }, { 0 }, 0, NULL, NULL }

/**
 * @brief Initializes the processor environment for the calling processor, this also
 * sets the calling TXU as the boot-TXU and initializes static data for this.
 *
 * @param[In] cpu  A pointer to the CPU structure that should be initialized.
 * @param[In] core A pointer to the PerCpu structure that should be pre-allocated before this call.
 */
KERNELAPI void KERNELABI
CpuInitializePlatform(
        _In_ SystemCpu_t*     cpu,
        _In_ SystemCpuCore_t* core);

/**
 * @brief If multiple cores are present this will boot them up. If multiple domains are present
 * it will boot all primary cores in each domain, then boot up rest of cores in our own domain.
 */
KERNELAPI void KERNELABI
CpuEnableMultiProcessorMode(void);

/**
 * @brief Registers a new cpu application core for the given cpu. The core count and the
 * application-core will be initialized on first call to this function. This also allocates a 
 * new instance of the cpu-core.
 */
KERNELAPI void KERNELABI
CpuCoreRegister(
        _In_ SystemCpu_t*     cpu,
        _In_ uuid_t           coreId,
        _In_ SystemCpuState_t initialState,
        _In_ int              external);

/**
 * @brief Activates the given core and prepares it for usage. This sets up a new
 * scheduler and initializes a new idle thread. This function does never return.
 */
KERNELAPI void KERNELABI
CpuCoreStart(void);

/**
 * StartApplicationCore (@arch)
 * Initializes and starts the cpu core given. This is called by the kernel if it detects multiple
 * cores in the processor.
 */
KERNELAPI void KERNELABI
StartApplicationCore(
    _In_ SystemCpuCore_t* core);

/**
 * TxuMessageSend
 * * Sends a TXU message to the target TXU.
 */
KERNELAPI oscode_t KERNELABI
TxuMessageSend(
        _In_ uuid_t                  CoreId,
        _In_ SystemCpuFunctionType_t Type,
        _In_ TxuFunction_t           Function,
        _In_ void*                   Argument,
        _In_ int                     Asynchronous);

/**
 * ProcessorMessageSend 
 * * Sends a message to an entire processor, and returns the number of TXU's
 * * the function request was successfully sent to.
 * @param ExcludeSelf [In] Whether or not to send the message to the calling TXU.
 * @param Type        [In] The message type
 * @param Message     [In] The message that should be sent
 */
KERNELAPI int KERNELABI
ProcessorMessageSend(
    _In_ int                     ExcludeSelf,
    _In_ SystemCpuFunctionType_t Type,
    _In_ TxuFunction_t           Function,
    _In_ void*                   Argument,
    _In_ int                     Asynchronous);

/**
 * GetProcessorCore
 * Retrieves the cpu core from the given core-id.
 */
KERNELAPI SystemCpuCore_t* KERNELABI
GetProcessorCore(
        _In_ uuid_t coreId);

/**
 * CpuCoreCurrent
 * Retrieves the cpu core that belongs to calling cpu core.
 */
KERNELAPI SystemCpuCore_t* KERNELABI
CpuCoreCurrent(void);

/**
 * CpuCoreEnterInterrupt
 * @param InterruptContext  The context of the current interrupt
 * @param InterruptPriority The priority before entering the interrupt
 */
KERNELAPI void KERNELABI
CpuCoreEnterInterrupt(
    _In_ Context_t* InterruptContext,
    _In_ int        InterruptPriority);

/**
 * CpuCoreExitInterrupt
 * @param InterruptContext  The context of the current interrupt
 * @param InterruptPriority The priority before entering the interrupt
 * @return                  The context that should be switched to
 */
KERNELAPI Context_t* KERNELABI
CpuCoreExitInterrupt(
    _In_ Context_t* InterruptContext,
    _In_ int        InterruptPriority);

/**
 * CpuCorePopQueuedIpc
 * @param cpuCore A pointer to a cpu core structure
 * @return        The next IPC structure that has been queued, or NULL if none
 */
KERNELAPI element_t* KERNELABI
CpuCorePopQueuedIpc(
        _In_ SystemCpuCore_t* cpuCore);

/**
 * CpuCoreQueueIpc
 * @param cpuCore A pointer to a cpu core structure
 * @param element A pointer to the element header of the IPC message
 */
KERNELAPI void KERNELABI
CpuCoreQueueIpc(
        _In_ SystemCpuCore_t*        cpuCore,
        _In_ SystemCpuFunctionType_t functionType,
        _In_ element_t*              element);

/**
 * CpuCoreId
 * @param cpuCore A pointer to a cpu core structure
 * @return        The id of the cpu core instance
 */
KERNELAPI uuid_t KERNELABI
CpuCoreId(
        _In_ SystemCpuCore_t* cpuCore);

/**
 * CpuCoreSetState
 * @param cpuCore A pointer to a cpu core structure
 * @param cpuState   The new state of the cpu core
 */
KERNELAPI void KERNELABI
CpuCoreSetState(
        _In_ SystemCpuCore_t* cpuCore,
        _In_ SystemCpuState_t cpuState);

/**
 * CpuCoreState
 * @param cpuCore A pointer to a cpu core structure
 * @return        The current cpu core state
 */
KERNELAPI SystemCpuState_t KERNELABI
CpuCoreState(
        _In_ SystemCpuCore_t* cpuCore);

/**
 * @brief Retrieves the next core link. Can be used to iterate through cores on a processor.
 *
 * @param cpuCore A pointer to a cpu core structure
 * @return        A pointer to the next cpu core in the cpu
 */
KERNELAPI SystemCpuCore_t* KERNELABI
CpuCoreNext(
        _In_ SystemCpuCore_t* cpuCore);

/**
 * @brief Retrieves the platform specific core data block
 *
 * @param[In] cpuCore A pointer to a cpu core structure
 * @return    A pointer to the platform cpu core block structure.
 */
KERNELAPI PlatformCpuCoreBlock_t* KERNELABI
CpuCorePlatformBlock(
        _In_ SystemCpuCore_t* cpuCore);

/**
 * CpuCoreSetPriority
 * @param cpuCore A pointer to a cpu core structure
 * @param priority The new priority of the cpu core
 */
KERNELAPI void KERNELABI
CpuCoreSetPriority(
        _In_ SystemCpuCore_t* cpuCore,
        _In_ int              priority);

/**
 * CpuCorePriority
 * @param cpuCore A pointer to a cpu core structure
 * @return        The current cpu core priority
 */
KERNELAPI int KERNELABI
CpuCorePriority(
        _In_ SystemCpuCore_t* cpuCore);

/**
 * CpuCoreSetCurrentThread
 * @param cpuCore A pointer to a cpu core structure
 */
KERNELAPI void KERNELABI
CpuCoreSetCurrentThread(
        _In_ SystemCpuCore_t* cpuCore,
        _In_ Thread_t*        thread);

/**
 * CpuCoreCurrentThread
 * @param cpuCore A pointer to a cpu core structure
 * @return        A pointer to the current thread instance
 */
KERNELAPI Thread_t* KERNELABI
CpuCoreCurrentThread(
        _In_ SystemCpuCore_t* cpuCore);

/**
 * CpuCoreIdleThread
 * @param cpuCore A pointer to a cpu core structure
 * @return        A pointer to the idle thread of the cpu core
 */
KERNELAPI Thread_t* KERNELABI
CpuCoreIdleThread(
        _In_ SystemCpuCore_t* cpuCore);

/**
 * CpuCoreScheduler
 * @param cpuCore A pointer to a cpu core structure
 * @return        A pointer to the cpu core's scheduler
 */
KERNELAPI Scheduler_t* KERNELABI
CpuCoreScheduler(
        _In_ SystemCpuCore_t* cpuCore);

/**
 * CpuCoreSetInterruptContext
 * @param cpuCore A pointer to a cpu core structure
 * @param context A pointer to the new interrupt context
 */
KERNELAPI void KERNELABI
CpuCoreSetInterruptContext(
        _In_ SystemCpuCore_t* cpuCore,
        _In_ Context_t*       context);

/**
 * CpuCoreInterruptContext
 * @param cpuCore A pointer to a cpu core structure
 * @return        A pointer to the current threads interrupt context
 */
KERNELAPI Context_t* KERNELABI
CpuCoreInterruptContext(
        _In_ SystemCpuCore_t* cpuCore);

#endif // !__COMPONENT_CPU__

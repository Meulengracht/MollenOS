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
 * @brief Sets up the boot processor and registers its first core.
 *
 * Call during platform startup with both records allocated. The architecture code
 * fills in the processor details and core ID before the core is added to the table.
 *
 * @param cpu Processor record to initialize.
 * @param core Preallocated record for the calling boot core.
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
 * @brief Allocates and registers another core in a processor's core list.
 *
 * Call during processor discovery after the boot core has been installed and the
 * processor reports multiple cores. coreId must be unused and below __CPU_MAX_COUNT.
 *
 * @param cpu Processor whose core list receives the new record.
 * @param coreId Hardware core ID used for direct lookup.
 * @param initialState Core state before startup.
 * @param external Nonzero for a core outside the current domain.
 */
KERNELAPI void KERNELABI
CpuCoreRegister(
        _In_ SystemCpu_t*     cpu,
        _In_ uuid_t           coreId,
        _In_ SystemCpuState_t initialState,
        _In_ int              external);

/**
 * @brief Sets up the calling core's memory space and idle thread, then runs it.
 *
 * The core must already be registered and have a working boot stack. Startup
 * installs per-core data, enables interrupts and scheduling, then enters the idle
 * loop. A domain's first core also starts the remaining cores in that domain.
 * Does not return.
 */
KERNELAPI void KERNELABI
CpuCoreStart(void);

/**
 * @brief Asks the architecture code to start a registered secondary core.
 *
 * @param core Core record with the platform startup data already filled in.
 */
KERNELAPI void KERNELABI
StartApplicationCore(
    _In_ SystemCpuCore_t* core);

/**
 * @brief Queues a callback request and sends an interrupt to a registered core.
 *
 * Interrupt handlers and the message cache must be initialized. Function and its
 * Argument must be usable on the target core. Keep any pointed-to data valid until
 * the callback is done. Delivered is set before the handler runs, so waiting for
 * it does not guarantee that the callback has finished.
 *
 * The message is queued before the interrupt is sent. A delivery error leaves it
 * queued. Callbacks that release references must still do so if they run later.
 *
 * @param CoreId Registered destination core.
 * @param Type Interrupt and queue to use. CpuFunctionCustom executes Function.
 * @param Function Callback to invoke on the destination core. Must not be NULL.
 * @param Argument Value passed to Function without copying pointed-to data.
 * @param Asynchronous Nonzero to return after sending. Zero waits for the core
 *                     to take the message, with a bounded wait that logs a timeout.
 * @return The interrupt delivery status. A wait timeout does not change that status.
 */
KERNELAPI oserr_t KERNELABI
TxuMessageSend(
        _In_ uuid_t                  CoreId,
        _In_ SystemCpuFunctionType_t Type,
        _In_ TxuFunction_t           Function,
        _In_ void*                   Argument,
        _In_ int                     Asynchronous);

/**
 * @brief Sends the same callback request to running cores in the current domain.
 *
 * Without a current domain, uses the machine's processor core list. The callback
 * may run on several cores, so shared Argument data needs suitable synchronization.
 * The data lifetime and completion rules are those of TxuMessageSend.
 *
 * @param ExcludeSelf Nonzero to skip the calling core.
 * @param Type Interrupt and queue to use.
 * @param Function Callback to send. Must not be NULL.
 * @param Argument Value passed unchanged to every callback.
 * @param Asynchronous Nonzero to send without waiting for each core to take its message.
 * @return Number of sends that returned OS_EOK.
 */
KERNELAPI int KERNELABI
ProcessorMessageSend(
    _In_ int                     ExcludeSelf,
    _In_ SystemCpuFunctionType_t Type,
    _In_ TxuFunction_t           Function,
    _In_ void*                   Argument,
    _In_ int                     Asynchronous);

/**
 * @brief Looks up a core in the table populated during processor startup.
 *
 * @param coreId Core ID below __CPU_MAX_COUNT.
 * @return The registered record, or NULL if that table entry is unused.
 */
KERNELAPI SystemCpuCore_t* KERNELABI
GetProcessorCore(
        _In_ uuid_t coreId);

/**
 * @brief Looks up the calling core using its hardware core ID.
 *
 * @return The core record, or NULL before that core has been registered.
 */
KERNELAPI SystemCpuCore_t* KERNELABI
CpuCoreCurrent(void);

/**
 * @brief Records an interrupt entry and finishes any thread awaiting stack cleanup.
 *
 * Call on a registered core with interrupts disabled, before running its handler.
 * The outer interrupt saves the register context and previous priority. A nested
 * interrupt only increases the nesting count, since it may still be using the
 * stack of the outgoing thread.
 *
 * At the next outer entry, the preceding interrupt has returned on the selected
 * thread's stack. PendingFinish is cleared before ThreadingFinish is called, so
 * that thread is finished once. Nested interrupts are enabled afterwards when
 * configured. Pair every entry with CpuCoreExitInterrupt.
 *
 * @param InterruptContext Registers saved at interrupt entry.
 * @param InterruptPriority Priority to restore when this interrupt returns.
 */
KERNELAPI void KERNELABI
CpuCoreEnterInterrupt(
    _In_ Context_t* InterruptContext,
    _In_ int        InterruptPriority);

/**
 * @brief Records a terminating thread to finish at the next outer interrupt entry.
 *
 * Call from ThreadingAdvance on the current core with interrupts disabled, after
 * selecting the next thread. The outgoing thread must be TERMINATING and removed
 * from scheduling. Its kernel stack is still used by the current interrupt return,
 * so releasing the scheduler reference here could allow that stack to be freed.
 *
 * Only one thread can wait in PendingFinish. The slot must be empty on entry.
 * CpuCoreEnterInterrupt clears it and calls ThreadingFinish once the old stack is
 * unused. This call adds no reference.
 *
 * @param core The calling core.
 * @param threadHandle Outgoing thread ID. The core becomes responsible for passing
 *                     its existing scheduler reference to ThreadingFinish.
 */
KERNELAPI void KERNELABI
CpuCoreDeferFinish(
    _In_ SystemCpuCore_t* core,
    _In_ uuid_t           threadHandle);

/**
 * @brief Leaves an interrupt and selects the register context to restore.
 *
 * Must match an earlier CpuCoreEnterInterrupt on this core. A nested return uses
 * its supplied context and priority. The outer return uses the core's saved
 * context, which the scheduler may have changed to run another thread.
 *
 * @param InterruptContext Registers saved for this interrupt.
 * @param InterruptPriority Priority saved before this interrupt.
 * @return Context for the architecture code to restore during interrupt return.
 */
KERNELAPI Context_t* KERNELABI
CpuCoreExitInterrupt(
    _In_ Context_t* InterruptContext,
    _In_ int        InterruptPriority);

/**
 * @brief Removes the next custom callback message from a core's queue.
 *
 * @param cpuCore Core whose CpuFunctionCustom queue is read, or NULL.
 * @return The message's list element, or NULL for an empty queue or NULL core.
 */
KERNELAPI element_t* KERNELABI
CpuCorePopQueuedIpc(
        _In_ SystemCpuCore_t* cpuCore);

/**
 * @brief Adds a message to the selected core queue without sending an interrupt.
 *
 * Keep the message allocated until the destination has processed it.
 *
 * @param cpuCore Destination core. NULL does nothing.
 * @param functionType Queue index below CpuFunctionCount.
 * @param element Message list element to insert.
 */
KERNELAPI void KERNELABI
CpuCoreQueueIpc(
        _In_ SystemCpuCore_t*        cpuCore,
        _In_ SystemCpuFunctionType_t functionType,
        _In_ element_t*              element);

/**
 * @brief Reads the hardware core ID stored during registration.
 *
 * @param cpuCore Core to inspect, or NULL.
 * @return The core ID, or UUID_INVALID for NULL.
 */
KERNELAPI uuid_t KERNELABI
CpuCoreId(
        _In_ SystemCpuCore_t* cpuCore);

/**
 * @brief Replaces the stored core state and its status bits.
 *
 * @param cpuCore Core to update. NULL does nothing.
 * @param cpuState Complete new state, including any status bits to keep.
 */
KERNELAPI void KERNELABI
CpuCoreSetState(
        _In_ SystemCpuCore_t* cpuCore,
        _In_ SystemCpuState_t cpuState);

/**
 * @brief Reads the stored core state and status bits.
 *
 * @param cpuCore Core to inspect, or NULL.
 * @return The state, or CpuStateUnavailable for NULL.
 */
KERNELAPI SystemCpuState_t KERNELABI
CpuCoreState(
        _In_ SystemCpuCore_t* cpuCore);

/**
 * @brief Reads the next core in the processor's linked list.
 *
 * @param cpuCore Current core, or NULL.
 * @return The next core, or NULL at the end of the list or for NULL input.
 */
KERNELAPI SystemCpuCore_t* KERNELABI
CpuCoreNext(
        _In_ SystemCpuCore_t* cpuCore);

/**
 * @brief Reads the architecture-specific data stored inside a core record.
 *
 * @param cpuCore Core to inspect, or NULL.
 * @return Pointer to its platform data, or NULL. The core record contains the storage.
 */
KERNELAPI PlatformCpuCoreBlock_t* KERNELABI
CpuCorePlatformBlock(
        _In_ SystemCpuCore_t* cpuCore);

/**
 * @brief Stores the priority to apply when leaving the outer interrupt.
 *
 * This updates the core record. It does not immediately change interrupt hardware.
 *
 * @param cpuCore Core to update. NULL does nothing.
 * @param priority Priority value to store.
 */
KERNELAPI void KERNELABI
CpuCoreSetPriority(
        _In_ SystemCpuCore_t* cpuCore,
        _In_ int              priority);

/**
 * @brief Reads the priority stored for interrupt return.
 *
 * @param cpuCore Core to inspect, or NULL.
 * @return The stored priority, or zero for NULL.
 */
KERNELAPI int KERNELABI
CpuCorePriority(
        _In_ SystemCpuCore_t* cpuCore);

/**
 * @brief Records the selected thread and its scheduler priority.
 *
 * Call during startup or scheduling while the core cannot switch threads. This
 * sets the software record. The scheduler must also install the thread's address
 * space and register context before returning from the interrupt.
 *
 * @param cpuCore Core to update. NULL does nothing.
 * @param thread Selected thread with an existing scheduler reference.
 */
KERNELAPI void KERNELABI
CpuCoreSetCurrentThread(
        _In_ SystemCpuCore_t* cpuCore,
        _In_ Thread_t*        thread);

/**
 * @brief Reads the thread currently selected on a core.
 *
 * The pointer adds no reference and can change on the next scheduling step.
 * Callers inspecting another core must keep the result valid while using it.
 *
 * @param cpuCore Core to inspect, or NULL.
 * @return Selected thread, or NULL before threading starts or for NULL input.
 */
KERNELAPI Thread_t* KERNELABI
CpuCoreCurrentThread(
        _In_ SystemCpuCore_t* cpuCore);

/**
 * @brief Reads the idle thread record stored inside a core.
 *
 * @param cpuCore Core to inspect, or NULL.
 * @return Pointer to the embedded record, or NULL. ThreadingEnable initializes it.
 */
KERNELAPI Thread_t* KERNELABI
CpuCoreIdleThread(
        _In_ SystemCpuCore_t* cpuCore);

/**
 * @brief Reads the scheduler data stored inside a core.
 *
 * @param cpuCore Core to inspect, or NULL.
 * @return Pointer to the embedded scheduler, or NULL.
 */
KERNELAPI Scheduler_t* KERNELABI
CpuCoreScheduler(
        _In_ SystemCpuCore_t* cpuCore);

/**
 * @brief Selects the saved registers to restore on the outer interrupt return.
 *
 * Call while handling an interrupt on this core, with scheduling changes protected
 * by disabling interrupts. This can select a different thread's saved context.
 *
 * @param cpuCore Core to update. NULL does nothing.
 * @param context Saved register context to install for return.
 */
KERNELAPI void KERNELABI
CpuCoreSetInterruptContext(
        _In_ SystemCpuCore_t* cpuCore,
        _In_ Context_t*       context);

/**
 * @brief Reads the context selected for the outer interrupt return.
 *
 * @param cpuCore Core to inspect, or NULL.
 * @return Saved register context, or NULL outside an outer interrupt or for NULL input.
 */
KERNELAPI Context_t* KERNELABI
CpuCoreInterruptContext(
        _In_ SystemCpuCore_t* cpuCore);

#endif // !__COMPONENT_CPU__

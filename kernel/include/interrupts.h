/**
 * MollenOS
 *
 * Copyright, Philip Meulengracht
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
 */

#ifndef _MCORE_INTERRUPTS_H_
#define _MCORE_INTERRUPTS_H_

#include <interrupt_types.h>
#include <os/osdefs.h>
#include <os/context.h>

/**
 * One reserved interrupt and the state used to run or remove its handler.
 * Several entries can share a hardware line. Group entries also hold the
 * provider callback and point to the group's shared enable flag.
 */
typedef struct SystemInterrupt {
    uuid_t                   Id;
    uuid_t                   Owner;
    uuid_t                   Thread;
    InterruptResourceTable_t KernelResources;
    // The group owns the callback reference. This entry borrows the pointer;
    // unbind clears it after any running callback finishes.
    struct InterruptCompletion* Completion;
    InterruptHandler_t       Handler;
    void*                    Context;
    unsigned int             Flags;
    InterruptControllerResource_t Routing;
    // A disabled entry can stay reserved to absorb late messages. Remember a
    // successful controller release so a retry does not release it twice.
    int                      ControllerReleased;
    // Disabled entries stay in the table. Dispatch checks Enabled before reading
    // the handler, which lets other entries on a shared line keep running.
    _Atomic(int)             Enabled;
    // Shared group enable flag, or NULL for a standalone kernel interrupt.
    _Atomic(int)*            GroupEnabled;
    int                      Bound;
    // Cleanup may leave some mappings behind. Never enable this handler then.
    int                      Unbinding;
    // Allows one update while the table lock is released to map resources or
    // wait for handlers. References keeps the entry alive during that time.
    int                      Updating;
    // InterruptGet() references keep a removed entry alive until callers release it.
    _Atomic(unsigned int)    References;
    // The interrupt path reads Link without the table lock. Keep it valid until
    // every table walk that could have seen it has finished.
    _Atomic(struct SystemInterrupt*) Link;
    struct SystemInterrupt*          RetiredLink;
    // Counts this reservation against the slot until active table walks finish.
    int                              HasPenalty;
} SystemInterrupt_t;

// OS Initialization
KERNELAPI void KERNELABI InitializeInterruptTable(void);
KERNELAPI void KERNELABI InitializeInterruptHandlers(void);

/**
 * Retrieves the system fast interrupt resource table to pass to process interrupt handlers.
 * @return A pointer to the system interrupt function table.
 */
KERNELAPI InterruptFunctionTable_t* KERNELABI
GetFastInterruptTable(void);

/**
 * @brief Reserve an interrupt without enabling it.
 * This claims the requested interrupt for a handler.
 */
KERNELAPI oserr_t KERNELABI
InterruptReserve(
    _InOut_ InterruptDescriptor_t* descriptor,
    _In_    unsigned int           flags,
    _Out_   uuid_t*                idOut);

/**
 * @brief Get a controller's message-slot capacity without reserving slots.
 * 
 * @param domain Controller ID. Zero selects the platform controller for kernel
 *        handlers.
 * @param kernelHandlers Nonzero if the handler runs in the kernel.
 * @param capacityOut Receives the total slot count, including slots in use.
 * @return OS_EOK on success, or an error if the controller cannot be found.
 */
KERNELAPI oserr_t KERNELABI
InterruptGetMessageCapacity(
    _In_  uuid_t    domain,
    _In_  int       kernelHandlers,
    _Out_ uint32_t* capacityOut);

/**
 * @brief Reserve a disabled message interrupt for exclusive use.
 *
 * @param domain Controller ID. Zero selects the platform controller for kernel
 *        handlers.
 * @param ordinal Zero-based message slot, below the controller's capacity.
 * @param flags Handler options. The core adds the message and exclusive-use flags.
 * @param idOut Receives the reservation ID, or UUID_INVALID on failure.
 * @param messageOut Receives the hardware message. It stays the same afterward.
 * @return OS_EOK, OS_EBUSY if the slot is in use, or another reservation error.
 */
KERNELAPI oserr_t KERNELABI
InterruptReserveMessage(
    _In_  uuid_t              domain,
    _In_  uint32_t            ordinal,
    _In_  unsigned int        flags,
    _Out_ uuid_t*             idOut,
    _Out_ InterruptMessage_t* messageOut);

/**
 * @brief Release a reservation or keep its message slot reserved after removing the handler.
 * @param id Reservation ID.
 * @param retire Nonzero keeps the disabled message slot reserved until reboot.
 * @return OS_EOK or an error. On failure, ownership stays in place for retry.
 * Stop the device from sending messages before calling this function.
 */
KERNELAPI oserr_t KERNELABI
InterruptRelease(
    _In_ uuid_t id,
    _In_ int    retire);

/**
 * @brief Release a dead process's reservation without changing its owner.
 *
 * @param id Reservation in a group held busy by InterruptGroupReap.
 * @param retire Nonzero keeps the message slot reserved so delayed device writes
 *        cannot reach another handler.
 * @return OS_EBUSY for a live owner, or the cleanup result. Keep the handle and
 *         retry if cleanup fails.
 */
KERNELAPI oserr_t KERNELABI
InterruptReap(
    _In_ uuid_t id,
    _In_ int    retire);

/**
 * @brief Return the process identity shared by all of the caller's threads.
 *
 * @return Process security-context handle, or UUID_INVALID without a context.
 */
KERNELAPI uuid_t KERNELABI
InterruptCurrentOwner(void);

/**
 * @brief Map a handler's resources to an interrupt that is already reserved.
 */
KERNELAPI oserr_t KERNELABI
InterruptBind(
    _In_ uuid_t                    id,
    _In_ InterruptResourceTable_t* resources);

/**
 * @brief Enable or disable a reserved interrupt.
 * Disabling waits for any running handlers to finish. A shared interrupt stays
 * enabled while at least one handler still needs it. Do not call from an
 * interrupt handler.
 */
KERNELAPI oserr_t KERNELABI
InterruptSetEnabled(
    _In_ uuid_t id,
    _In_ int    enable);

/**
 * @brief Remove a handler's resource mappings but keep its interrupt reserved.
 * A driver can then change its setup without giving up the interrupt. If
 * cleanup fails, retry Unbind before enabling the interrupt or binding another handler.
 */
KERNELAPI oserr_t KERNELABI
InterruptUnbind(
    _In_ uuid_t id);

/* InterruptRegister
 * Tries to allocate the given interrupt source by the given descriptor and flags. On success
 * it returns the id of the irq, and on failure it returns UUID_INVALID */
KERNELAPI uuid_t KERNELABI
InterruptRegister(
    _In_ InterruptDescriptor_t* deviceInterrupt,
    _In_ unsigned int           flags);

/* InterruptUnregister 
 * Unregisters the interrupt from the system and removes any resources that was associated 
 * with that interrupt also masks the interrupt if it was the only user */
KERNELAPI oserr_t KERNELABI
InterruptUnregister(
        _In_ uuid_t Source);

/**
 * @brief Acquires a reference to the system interrupt described
 * by its id. Use Put to release the reference.
 */
KERNELAPI SystemInterrupt_t* KERNELABI
InterruptGet(
    _In_ uuid_t Source);

/* InterruptPut
 * Releases a reference acquired by InterruptGet(). */
KERNELAPI void KERNELABI
InterruptPut(
    _In_ SystemInterrupt_t* Interrupt);

/* InterruptSetActiveStatus
 * Set's the current status for the calling cpu to interrupt-active state */
KERNELAPI void KERNELABI
InterruptSetActiveStatus(
    _In_ int Active);

/* InterruptGetActiveStatus
 * Get's the current status for the calling cpu to interrupt-active state */
KERNELAPI int KERNELABI
InterruptGetActiveStatus(void);

/* InterruptHandle
 * Handles an interrupt by invoking the registered handlers on the given table-index. */
KERNELAPI Context_t* KERNELABI
InterruptHandle(
    _In_  Context_t* context,
    _In_  int        tableIndex);

/**
 * @brief Handles a interrupt directly without entering CPU interrupt context. Useful
 * for some specific interrupt controllers.
 */
KERNELAPI void KERNELABI
InterruptHandleDirect(
    _In_ uint32_t tableIndex);

/* InterruptIncreasePenalty 
 * Increases the penalty for an interrupt source. This affects how the system allocates
 * interrupts when load balancing */
KERNELAPI oserr_t KERNELABI
InterruptIncreasePenalty(
    _In_ int Source);

/* InterruptDecreasePenalty 
 * Decreases the penalty for an interrupt source. This affects how the system allocates
 * interrupts when load balancing */
KERNELAPI oserr_t KERNELABI
InterruptDecreasePenalty(
    _In_ int Source);

/**
 * Retrieves the penalty for an interrupt source.
 * @param Source
 * @return If INTERRUPT_NONE is returned the source is unavailable.
 */
KERNELAPI int KERNELABI
InterruptGetPenalty(
    _In_ int Source);

/**
 * Out of the requested interrupt vectors, the least loaded interrupt vector is returned.
 * @param interruptVectors Available interrupt vectors to select.
 * @param count            Number of interrupt vectors.
 * @return                 The least loaded interrupt vector.
 */
KERNELAPI int KERNELABI
InterruptGetLeastLoaded(
    _In_ int interruptVectors[],
    _In_ int count);

/**
 * Returns whether or not the polarity is Active Low or Active High.
 * @param intiFlags
 * @param source
 * @return  For Active Low = 1, Active High = 0
 */
KERNELAPI int KERNELABI
AcpiGetPolarityMode(
    _In_ uint16_t intiFlags,
    _In_ int      source);

/**
 * Returns whether or not the trigger mode of the interrup is level or edge.
 * @param intiFlags
 * @param source
 * @return For Level = 1, Edge = 0
 */
KERNELAPI int KERNELABI
AcpiGetTriggerMode(
    _In_ uint16_t intiFlags,
    _In_ int      source);

/**
 * Converts acpi interrupt flags to the system interrupt conform flags.
 * @param intiFlags
 * @param source
 * @return
 */
KERNELAPI unsigned int KERNELABI
ConvertAcpiFlagsToConformFlags(
    _In_ uint16_t intiFlags,
    _In_ int      source);

/**
 * Derives an interrupt by extracting routings from the bus of the device
 * @param bus
 * @param device
 * @param pciPin
 * @param interruptOut
 * @param acpiConformOut
 * @return
 */
KERNELAPI oserr_t KERNELABI
AcpiDeviceGetInterrupt(
        _In_  int           bus,
        _In_  int           device,
        _In_  int           pciPin,
        _Out_ int*          interruptOut,
        _Out_ unsigned int* acpiConformOut);

#endif //!_MCORE_INTERRUPTS_H_

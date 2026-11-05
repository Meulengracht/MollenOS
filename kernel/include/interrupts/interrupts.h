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

#include <interrupts/types.h>
#include <os/osdefs.h>
#include <os/context.h>

/**
 * @brief A reserved interrupt and the state used to run or remove its handler.
 * Several entries can share a hardware line. Group entries also hold the
 * bus callback and point to the group's shared enable flag.
 */
typedef struct SystemInterrupt {
    uuid_t                   Id;
    uuid_t                   Owner;
    uuid_t                   Thread;
    InterruptResourceTable_t KernelResources;
    // The group holds the callback reference. Unbind clears this pointer after
    // any running callback finishes.
    struct InterruptBusHandler*      BusHandler;
    InterruptHandler_t               Handler;
    void*                            Context;
    unsigned int                     Flags;
    InterruptControllerResource_t    Routing;
    // A disabled entry can stay reserved to absorb late messages. Remember a
    // successful controller release so a retry does not release it twice.
    int                              ControllerReleased;
    // Disabled entries stay in the table. Dispatch checks Enabled before reading
    // the handler, which lets other entries on a shared line keep running.
    _Atomic(int)                     Enabled;
    // Shared group enable flag, or NULL for a standalone kernel interrupt.
    _Atomic(int)*                    GroupEnabled;
    int                              Bound;
    // If cleanup leaves mappings behind, it must finish before enabling the handler.
    int                              Unbinding;
    // Allows one update while the table lock is released to map resources or
    // wait for handlers. References keeps the entry alive during that time.
    int                              Updating;
    // InterruptGet() references keep a removed entry alive until callers release it.
    _Atomic(unsigned int)            References;
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
 * @brief Get the system function table passed to process interrupt handlers.
 *
 * @return A pointer to the system interrupt function table.
 */
KERNELAPI InterruptFunctionTable_t* KERNELABI
GetFastInterruptTable(void);

/**
 * @brief Reserve an interrupt without enabling it.
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
 * @brief Remove the handler and release or retire its reservation.
 * Stop the device from sending messages before calling this function.
 *
 * @param id Reservation ID.
 * @param retire Nonzero keeps the disabled message slot reserved until reboot.
 * @return OS_EOK or an error. On failure, the reservation stays with the caller
 *         so cleanup can be retried.
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
 * Must be called outside an interrupt handler.
 * Disabling waits for any running handlers to finish. A shared interrupt stays
 * enabled while at least one handler still needs it.
 */
KERNELAPI oserr_t KERNELABI
InterruptSetEnabled(
    _In_ uuid_t id,
    _In_ int    enable);

/**
 * @brief Remove a handler's resource mappings but keep its interrupt reserved.
 * A driver can then change its setup without giving up the interrupt. If
 * cleanup fails, retry InterruptUnbind before enabling the interrupt or binding
 * another handler.
 */
KERNELAPI oserr_t KERNELABI
InterruptUnbind(
    _In_ uuid_t id);

/**
 * @brief Register an interrupt using the given descriptor and flags.
 *
 * @return Interrupt ID on success, or UUID_INVALID on failure.
 */
KERNELAPI uuid_t KERNELABI
InterruptRegister(
    _In_ InterruptDescriptor_t* deviceInterrupt,
    _In_ unsigned int           flags);

/**
 * @brief Remove an interrupt and its resources.
 * Masks the interrupt if no other handlers use it.
 */
KERNELAPI oserr_t KERNELABI
InterruptUnregister(
        _In_ uuid_t Source);

/**
 * @brief Get a reference to an interrupt by ID.
 * Call InterruptPut to release the reference.
 */
KERNELAPI SystemInterrupt_t* KERNELABI
InterruptGet(
    _In_ uuid_t Source);

/**
 * @brief Release a reference returned by InterruptGet.
 */
KERNELAPI void KERNELABI
InterruptPut(
    _In_ SystemInterrupt_t* Interrupt);

/**
 * @brief Set the interrupt-active state for the calling CPU.
 */
KERNELAPI void KERNELABI
InterruptSetActiveStatus(
    _In_ int Active);

/**
 * @brief Get the interrupt-active state for the calling CPU.
 */
KERNELAPI int KERNELABI
InterruptGetActiveStatus(void);

/**
 * @brief Run the interrupt handlers registered at the given table index.
 */
KERNELAPI Context_t* KERNELABI
InterruptHandle(
    _In_  Context_t* context,
    _In_  int        tableIndex);

/**
 * @brief Run interrupt handlers directly without entering CPU interrupt context.
 */
KERNELAPI void KERNELABI
InterruptHandleDirect(
    _In_ uint32_t tableIndex);

/**
 * @brief Increase the source penalty used when balancing interrupt allocations.
 */
KERNELAPI oserr_t KERNELABI
InterruptIncreasePenalty(
    _In_ int Source);

/**
 * @brief Decrease the source penalty used when balancing interrupt allocations.
 */
KERNELAPI oserr_t KERNELABI
InterruptDecreasePenalty(
    _In_ int Source);

/**
 * @brief Get the penalty for an interrupt source.
 *
 * @param Source
 * @return If INTERRUPT_NONE is returned the source is unavailable.
 */
KERNELAPI int KERNELABI
InterruptGetPenalty(
    _In_ int Source);

/**
 * @brief Select the least loaded vector from the given interrupt vectors.
 *
 * @param interruptVectors Available interrupt vectors to select.
 * @param count            Number of interrupt vectors.
 * @return                 The least loaded interrupt vector.
 */
KERNELAPI int KERNELABI
InterruptGetLeastLoaded(
    _In_ int interruptVectors[],
    _In_ int count);

/**
 * @brief Get the interrupt polarity from the ACPI flags.
 *
 * @param intiFlags
 * @param source
 * @return 1 for active low, or 0 for active high.
 */
KERNELAPI int KERNELABI
AcpiGetPolarityMode(
    _In_ uint16_t intiFlags,
    _In_ int      source);

/**
 * @brief Get the interrupt trigger mode from the ACPI flags.
 *
 * @param intiFlags
 * @param source
 * @return 1 for level-triggered, or 0 for edge-triggered.
 */
KERNELAPI int KERNELABI
AcpiGetTriggerMode(
    _In_ uint16_t intiFlags,
    _In_ int      source);

/**
 * @brief Convert ACPI interrupt flags to system interrupt conform flags.
 *
 * @param intiFlags
 * @param source
 * @return
 */
KERNELAPI unsigned int KERNELABI
ConvertAcpiFlagsToConformFlags(
    _In_ uint16_t intiFlags,
    _In_ int      source);

/**
 * @brief Get the device interrupt from its bus routing.
 *
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

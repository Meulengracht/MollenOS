/* MollenOS
 *
 * Copyright 2011, Philip Meulengracht
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
 *
 * Interrupt Interface
 * - Contains the shared kernel interrupt interface
 *   that all sub-layers must conform to
 */

#ifndef _MCORE_INTERRUPTS_H_
#define _MCORE_INTERRUPTS_H_

#include <ddk/interrupt.h>
#include <os/osdefs.h>
#include <os/context.h>

// Kernel specific interrupt models
#define INTERRUPT_KERNEL 0x10000000U

/**
 * @brief Describes one kernel-owned source, including timers and CPU messages.
 * Drivers use allocation and bind requests instead, so their local arrays never
 * become part of the system call interface.
 */
typedef struct InterruptDescriptor {
    // Kernel handlers receive Context directly. User handlers instead receive
    // the resource table mapped by InterruptBind.
    InterruptResourceTable_t ResourceTable;
    void*                    Context;

    // InterruptResolve may replace Line with the platform's routed line number.
    unsigned int AcpiConform;
    int          Line;
    int          Pin;

    // Candidate lines for kernel sources such as HPET. This fixed-size list is
    // unrelated to DeviceInterrupt_t's dynamically allocated vector array.
    int Vectors[INTERRUPT_MAXVECTORS];

    // Existing kernel MSI users receive their hardware message here. Driver
    // MSI/MSI-X allocation is deliberately not implemented by this descriptor.
    uint64_t  MsiAddress;
    uintptr_t MsiValue;
} InterruptDescriptor_t;

/** Owns one reserved source and the mappings used while its handler is bound. */
typedef struct SystemInterrupt {
    uuid_t                   Id;
    uuid_t                   Owner;
    uuid_t                   Thread;
    InterruptResourceTable_t KernelResources;
    InterruptHandler_t       Handler;
    void*                    Context;
    unsigned int             Flags;
    unsigned int             AcpiConform;
    int                      Line;
    int                      Pin;
    int                      Source;
    // Reserved entries stay in the table while disabled. Dispatch checks Enabled
    // before reading their handler, so a shared line can remain active safely.
    _Atomic(int)             Enabled;
    int                      Bound;
    // Cleanup may have removed only some mappings; never enable that handler.
    int                      Unbinding;
    // Serializes operations that must release the table lock to map resources
    // or wait for running handlers. References keep the entry alive meanwhile.
    int                      Updating;
    // References acquired by InterruptGet() keep retired descriptors alive.
    _Atomic(unsigned int)    References;
    // Link is read by the interrupt path without taking the table lock. It
    // remains valid until the corresponding RCU grace period has completed.
    _Atomic(struct SystemInterrupt*) Link;
    struct SystemInterrupt*          RetiredLink;
    // Set while the entry holds a penalty; 
    // released after the grace period.
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
 * @brief Reserves one source without mapping a handler or enabling hardware.
 * The reservation participates in sharing checks even while disabled, so an
 * exclusive source cannot be claimed by a second caller during configuration.
 */
KERNELAPI oserr_t KERNELABI
InterruptReserve(
    _InOut_ InterruptDescriptor_t* descriptor,
    _In_    unsigned int           flags,
    _Out_   uuid_t*                idOut);

/**
 * @brief Maps resources while the reservation remains disabled.
 * Keeping mapping separate from delivery prevents a shared line from invoking
 * a handler before all of its requested resources are available.
 */
KERNELAPI oserr_t KERNELABI
InterruptBind(
    _In_ uuid_t                    id,
    _In_ InterruptResourceTable_t* resources);

/**
 * @brief Changes delivery and waits for running handlers when disabling.
 * A shared hardware line remains enabled until its last enabled user stops.
 * Call only from ordinary execution context, where waiting is permitted.
 */
KERNELAPI oserr_t KERNELABI
InterruptSetEnabled(
    _In_ uuid_t id,
    _In_ int    enable);

/**
 * @brief Releases handler mappings while retaining the disabled reservation.
 * This allows a driver to change configuration without losing its interrupt.
 * If cleanup fails, retry Unbind before enabling or binding another handler.
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
 * @brief References an entry to inspect its immutable identity and ownership.
 * Release the reference with InterruptPut. A reference keeps the descriptor
 * alive, but does not pin its handler mappings: those belong to the binding
 * lifecycle and may only be accessed by the protected dispatch path.
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

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
 * along with this program.If not, see <http://www.gnu.org/licenses/>.
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

typedef struct SystemInterrupt {
    UUId_t                   Id;
    UUId_t                   ModuleHandle;
    UUId_t                   Thread;
    InterruptResourceTable_t KernelResources;
    InterruptHandler_t       Handler;
    void*                    Context;
    unsigned int             Flags;
    unsigned int             AcpiConform;
    int                      Line;
    int                      Pin;
    int                      Source;
    struct SystemInterrupt*  Link;
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

/* InterruptRegister
 * Tries to allocate the given interrupt source by the given descriptor and flags. On success
 * it returns the id of the irq, and on failure it returns UUID_INVALID */
KERNELAPI UUId_t KERNELABI
InterruptRegister(
    _In_ DeviceInterrupt_t* deviceInterrupt,
    _In_ unsigned int       flags);

/* InterruptUnregister 
 * Unregisters the interrupt from the system and removes any resources that was associated 
 * with that interrupt also masks the interrupt if it was the only user */
KERNELAPI OsStatus_t KERNELABI
InterruptUnregister(
    _In_ UUId_t Source);

/* InterruptGet
 * Retrieves the given interrupt source information as a SystemInterrupt_t */
KERNELAPI SystemInterrupt_t* KERNELABI
InterruptGet(
   _In_ UUId_t Source);

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
    _In_  Context_t* Context,
    _In_  int        TableIndex);

/* InterruptIncreasePenalty 
 * Increases the penalty for an interrupt source. This affects how the system allocates
 * interrupts when load balancing */
KERNELAPI OsStatus_t KERNELABI
InterruptIncreasePenalty(
    _In_ int Source);

/* InterruptDecreasePenalty 
 * Decreases the penalty for an interrupt source. This affects how the system allocates
 * interrupts when load balancing */
KERNELAPI OsStatus_t KERNELABI
InterruptDecreasePenalty(
    _In_ int Source);

/* InterruptGetPenalty
 * Retrieves the penalty for an interrupt source. If INTERRUPT_NONE is returned the source is unavailable. */
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

/* AcpiGetPolarityMode
 * Returns whether or not the polarity is Active Low or Active High.
 * For Active Low = 1, Active High = 0 */
KERNELAPI int KERNELABI
AcpiGetPolarityMode(
    _In_ uint16_t IntiFlags,
    _In_ int      Source);

/* AcpiGetTriggerMode
 * Returns whether or not the trigger mode of the interrup is level or edge.
 * For Level = 1, Edge = 0 */
KERNELAPI int KERNELABI
AcpiGetTriggerMode(
    _In_ uint16_t IntiFlags,
    _In_ int      Source);

/* ConvertAcpiFlagsToConformFlags
 * Converts acpi interrupt flags to the system interrupt conform flags. */
KERNELAPI unsigned int KERNELABI
ConvertAcpiFlagsToConformFlags(
    _In_ uint16_t IntiFlags,
    _In_ int      Source);

/* AcpiDeriveInterrupt
 * Derives an interrupt by consulting the bus of the device, and spits out flags in
 * AcpiConform and returns irq */
KERNELAPI int KERNELABI
AcpiDeriveInterrupt(
    _In_  unsigned int  Bus,
    _In_  unsigned int  Device,
    _In_  int           Pin,
    _Out_ unsigned int* AcpiConform);

#endif //!_MCORE_INTERRUPTS_H_

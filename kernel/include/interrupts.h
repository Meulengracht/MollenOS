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
#define INTERRUPT_KERNEL 0x10000000

typedef struct SystemInterrupt {
    DeviceInterrupt_t        Interrupt;
    InterruptResourceTable_t KernelResources;
    UUId_t                   Id;
    UUId_t                          ModuleHandle;
    UUId_t                          Thread;
    unsigned int                         Flags;
    int                             Source;
    struct SystemInterrupt*         Link;
} SystemInterrupt_t;

/* InitializeInterruptTable
 * Initializes the static system interrupt table. This must be done before any driver interrupts
 * as they will rely on the system function table that gets passed along. */
KERNELAPI void KERNELABI
InitializeInterruptTable(void);

/* InitializeInterruptHandlers
 * Initializes the default kernel interrupt handlers for different multi-core functionality. */
KERNELAPI void KERNELABI
InitializeInterruptHandlers(void);

/* InterruptFunctionTable_t
 * Retrieves the system fast interrupt resource table to pass to process interrupt handlers. */
KERNELAPI InterruptFunctionTable_t* KERNELABI
GetFastInterruptTable(void);

/* InterruptRegister
 * Tries to allocate the given interrupt source by the given descriptor and flags. On success
 * it returns the id of the irq, and on failure it returns UUID_INVALID */
KERNELAPI UUId_t KERNELABI
InterruptRegister(
    _In_ DeviceInterrupt_t* Interrupt,
    _In_ unsigned int            Flags);

/* InterruptUnregister 
 * Unregisters the interrupt from the system and removes any resources that was associated 
 * with that interrupt also masks the interrupt if it was the only user */
KERNELAPI OsStatus_t KERNELABI
InterruptUnregister(
    _In_ UUId_t             Source);

/* InterruptGet
 * Retrieves the given interrupt source information as a SystemInterrupt_t */
KERNELAPI SystemInterrupt_t* KERNELABI
InterruptGet(
   _In_ UUId_t              Source);

/* InterruptGetIndex
 * Retrieves the given interrupt source information as a SystemInterrupt_t */
KERNELAPI SystemInterrupt_t* KERNELABI
InterruptGetIndex(
   _In_ UUId_t              TableIndex);

/* InterruptSetActiveStatus
 * Set's the current status for the calling cpu to interrupt-active state */
KERNELAPI void KERNELABI
InterruptSetActiveStatus(
    _In_ int                Active);

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
    _In_ int                Source);

/* InterruptDecreasePenalty 
 * Decreases the penalty for an interrupt source. This affects how the system allocates
 * interrupts when load balancing */
KERNELAPI OsStatus_t KERNELABI
InterruptDecreasePenalty(
    _In_ int                Source);

/* InterruptGetPenalty
 * Retrieves the penalty for an interrupt source. If INTERRUPT_NONE is returned the source is unavailable. */
KERNELAPI int KERNELABI
InterruptGetPenalty(
    _In_ int                Source);

/* InterruptGetLeastLoaded
 * Allocates the least used sharable irq
 * most useful for MSI devices */
KERNELAPI int KERNELABI
InterruptGetLeastLoaded(
    _In_ int                Irqs[],
    _In_ int                Count);

/* AcpiGetPolarityMode
 * Returns whether or not the polarity is Active Low or Active High.
 * For Active Low = 1, Active High = 0 */
KERNELAPI int KERNELABI
AcpiGetPolarityMode(
    _In_ uint16_t           IntiFlags,
    _In_ int                Source);

/* AcpiGetTriggerMode
 * Returns whether or not the trigger mode of the interrup is level or edge.
 * For Level = 1, Edge = 0 */
KERNELAPI int KERNELABI
AcpiGetTriggerMode(
    _In_ uint16_t           IntiFlags,
    _In_ int                Source);

/* ConvertAcpiFlagsToConformFlags
 * Converts acpi interrupt flags to the system interrupt conform flags. */
KERNELAPI unsigned int KERNELABI
ConvertAcpiFlagsToConformFlags(
    _In_ uint16_t           IntiFlags,
    _In_ int                Source);

/* AcpiDeriveInterrupt
 * Derives an interrupt by consulting the bus of the device, and spits out flags in
 * AcpiConform and returns irq */
KERNELAPI int KERNELABI
AcpiDeriveInterrupt(
    _In_  unsigned int         Bus, 
    _In_  unsigned int         Device,
    _In_  int               Pin,
    _Out_ unsigned int*          AcpiConform);

#endif //!_MCORE_INTERRUPTS_H_

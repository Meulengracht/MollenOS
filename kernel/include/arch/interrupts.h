/**
 * MollenOS
 *
 * Copyright 2017, Philip Meulengracht
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
 * Interrupts Interface
 * - Shared kernel interface used by each architecture's interrupt code.
 */

#ifndef __VALI_ARCH_INTERRUPT_H__
#define __VALI_ARCH_INTERRUPT_H__

#include <os/osdefs.h>
#include <interrupt_types.h>

/**
 * @brief Initializes the interrupts for the platform.
 *
 * @return Status of the initialization.
 */
KERNELAPI oserr_t KERNELABI
PlatformInterruptInitialize(void);

/**
 * @brief Specifies the current interrupt mode. This is currently used on X86 in conjunction with acpi.
 *
 * @param[In] mode 0 = Pic, 1 = Apic
 */
KERNELAPI void KERNELABI
InterruptSetMode(
    _In_ int mode);

/**
 *
 * @param deviceInterrupt
 * @param flags
 * @param tableIndex
 * @return
 */
KERNELAPI oserr_t KERNELABI
InterruptResolve(
    _In_    InterruptDescriptor_t* deviceInterrupt,
    _In_    unsigned int           flags,
    _Out_   uuid_t*                tableIndex);

/**
 * Set up, enable, or disable a controller route without changing its handler.
 * @param resource Controller and hardware route for the interrupt.
 * @param enable Nonzero to enable delivery, or zero to disable it.
 * @return OS_EOK or an error from the controller.
 */
KERNELAPI oserr_t KERNELABI
InterruptConfigure(
    _In_ const InterruptControllerResource_t* resource,
    _In_ int                                  enable);

/* InterruptsPriority
 * Set or get the current core task priority. Can be used to leverage hardware
 * prioritization for optimizing delivery of interrupts. */
KERNELAPI uint32_t KERNELABI InterruptsGetPriority(void);
KERNELAPI void     KERNELABI InterruptsSetPriority(uint32_t Priority);

/* InterruptsAcknowledge
 * Acknowledge interrupt with the source (interrupt line) and the table index, which
 * is the virtual table entry. */
KERNELAPI void KERNELABI InterruptsAcknowledge(int Source, uint32_t TableIndex);

KERNELAPI irqstate_t KERNELABI InterruptDisable(void);
KERNELAPI irqstate_t KERNELABI InterruptEnable(void);
KERNELAPI irqstate_t KERNELABI InterruptRestoreState(irqstate_t State);
KERNELAPI irqstate_t KERNELABI InterruptSaveState(void);
KERNELAPI int        KERNELABI InterruptIsDisabled(void);

/**
 * @brief Return the number of message-vector slots the platform can provide.
 */
KERNELAPI uint32_t KERNELABI InterruptMessageVectorCount(void);

/**
 * @brief Build the descriptor and hardware message for one message-vector slot.
 *
 * @param ordinal Zero-based slot number.
 * @param descriptor Receives the interrupt details.
 * @param message Receives the address and data for the device's message.
 * @return OS_EOK or an error if the platform cannot provide this slot.
 */
KERNELAPI oserr_t KERNELABI InterruptMessageVector(
    _In_ uint32_t               ordinal,
    _In_ InterruptDescriptor_t* descriptor,
    _In_ InterruptMessage_t*    message);


#endif //!__VALI_ARCH_INTERRUPT_H__

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
 * - Contains the shared kernel interrupts interface
 *   that all sub-layers / architectures must conform to
 */

#ifndef __VALI_ARCH_INTERRUPT_H__
#define __VALI_ARCH_INTERRUPT_H__

#include <os/osdefs.h>
#include <interrupts.h>

/* InterruptInitialize
 * Initialize interrupts in the base system. */
KERNELAPI OsStatus_t KERNELABI InterruptInitialize(void);

/**
 * Specifies the current interrupt mode. This is currently used on X86 in conjunction with acpi.
 * @param mode [In] 0 = Pic, 1 = Apic
 */
KERNELAPI void KERNELABI InterruptSetMode(
        _In_ int mode);

/**
 *
 * @param deviceInterrupt
 * @param flags
 * @param tableIndex
 * @return
 */
KERNELAPI OsStatus_t KERNELABI
InterruptResolve(
    _In_    DeviceInterrupt_t* deviceInterrupt,
    _In_    unsigned int       flags,
    _Out_   UUId_t*            tableIndex);

/**
 * Initialize and enable/disable the interrupt described in systemInterrupt member.
 * @param systemInterrupt
 * @param enable
 * @return Status of the operation
 */
KERNELAPI OsStatus_t KERNELABI
InterruptConfigure(
    _In_ SystemInterrupt_t* systemInterrupt,
    _In_ int                enable);

/* InterruptsPriority
 * Set or get the current core task priority. Can be used to leverage hardware
 * prioritization for optimizing delivery of interrupts. */
KERNELAPI uint32_t KERNELABI InterruptsGetPriority(void);
KERNELAPI void     KERNELABI InterruptsSetPriority(uint32_t Priority);

/* InterruptsAcknowledge
 * Acknowledge interrupt with the source (interrupt line) and the table index, which
 * is the virtual table entry. */
KERNELAPI void KERNELABI InterruptsAcknowledge(int Source, uint32_t TableIndex);

KERNELAPI IntStatus_t KERNELABI InterruptDisable(void);
KERNELAPI IntStatus_t KERNELABI InterruptEnable(void);
KERNELAPI IntStatus_t KERNELABI InterruptRestoreState(IntStatus_t State);
KERNELAPI IntStatus_t KERNELABI InterruptSaveState(void);
KERNELAPI int         KERNELABI InterruptIsDisabled(void);

#endif //!__VALI_ARCH_INTERRUPT_H__

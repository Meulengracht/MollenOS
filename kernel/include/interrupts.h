/* MollenOS
 *
 * Copyright 2011 - 2017, Philip Meulengracht
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
 * MollenOS Interrupt Interface
 * - Contains the shared kernel interrupt interface
 *   that all sub-layers must conform to
 */

#ifndef _MCORE_INTERRUPTS_H_
#define _MCORE_INTERRUPTS_H_

/* Includes 
 * - System */
#include <os/driver/interrupt.h>
#include <os/osdefs.h>

/* Structures */
typedef struct _MCoreInterruptDescriptor {
	MCoreInterrupt_t					Interrupt;
	UUId_t								Id;
	UUId_t								Ash;
	UUId_t								Thread;
	Flags_t								Flags;
	int									Source;
	struct _MCoreInterruptDescriptor	*Link;
} MCoreInterruptDescriptor_t;

/* InterruptInitialize
 * Initializes the interrupt-manager code
 * and initializes all the resources for
 * allocating and freeing interrupts */
__CRT_EXTERN void InterruptInitialize(void);

/* InterruptRegister
 * Tries to allocate the given interrupt source
 * by the given descriptor and flags. On success
 * it returns the id of the irq, and on failure it
 * returns UUID_INVALID */
__CRT_EXTERN UUId_t InterruptRegister(MCoreInterrupt_t *Interrupt, Flags_t Flags);

/* InterruptUnregister 
 * Unregisters the interrupt from the system and removes
 * any resources that was associated with that interrupt 
 * also masks the interrupt if it was the only user */
__CRT_EXTERN OsStatus_t InterruptUnregister(UUId_t Source);

__CRT_EXTERN int InterruptAllocateISA(uint32_t Irq);
__CRT_EXTERN void InterruptInstallISA(uint32_t Irq, uint32_t IdtEntry, IrqHandler_t Callback, void *Args);/* Install PCI Interrupt */
__CRT_EXTERN void InterruptInstallIdtOnly(int Gsi, uint32_t IdtEntry, IrqHandler_t Callback, void *Args);
__CRT_EXTERN uint32_t InterruptAllocatePCI(uint32_t Irqs[], uint32_t Count);

__CRT_EXTERN IntStatus_t InterruptDisable(void);
__CRT_EXTERN IntStatus_t InterruptEnable(void);
__CRT_EXTERN IntStatus_t InterruptSaveState(void);
__CRT_EXTERN IntStatus_t InterruptRestoreState(IntStatus_t state);

/* Helpers */
__CRT_EXTERN Flags_t InterruptGetPolarity(uint16_t IntiFlags, uint8_t IrqSource);
__CRT_EXTERN Flags_t InterruptGetTrigger(uint16_t IntiFlags, uint8_t IrqSource);

#endif //!_MCORE_INTERRUPTS_H_
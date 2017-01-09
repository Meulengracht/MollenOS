/* MollenOS
*
* Copyright 2011 - 2014, Philip Meulengracht
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
* MollenOS x86 Interrupts
*/

#ifndef _X86_INTERRUPTS_H_
#define _X86_INTERRUPTS_H_

/* Includes */
#include <Arch.h>
#include <os/osdefs.h>

/* Definitions */
#ifndef __IRQHANDLER
#define __IRQHANDLER
typedef int(*IrqHandler_t)(void*);
#endif

/* Irq Return Codes */
#define X86_IRQ_NOT_HANDLED			(int)0x0
#define X86_IRQ_HANDLED				(int)0x1

#define X86_MAX_HANDLERS_PER_INTERRUPT	4
#define X86_NUM_ISA_INTERRUPTS			16

/* Structures */
typedef struct _IrqEntry
{
	/* The Irq function */
	IrqHandler_t Function;

	/* Associated Data */
	void *Data;

	/* Whether it's installed or not */
	int Installed;

	/* Pin */
	int Gsi;

} IrqEntry_t;

/* Prototypes */
__CRT_EXTERN void InterruptInit(void);
__CRT_EXTERN int InterruptAllocateISA(uint32_t Irq);
__CRT_EXTERN void InterruptInstallISA(uint32_t Irq, uint32_t IdtEntry, IrqHandler_t Callback, void *Args);/* Install PCI Interrupt */
__CRT_EXTERN void InterruptInstallIdtOnly(int Gsi, uint32_t IdtEntry, IrqHandler_t Callback, void *Args);
__CRT_EXTERN uint32_t InterruptAllocatePCI(uint32_t Irqs[], uint32_t Count);

_CRT_EXPORT IntStatus_t InterruptDisable(void);
_CRT_EXPORT IntStatus_t InterruptEnable(void);
_CRT_EXPORT IntStatus_t InterruptSaveState(void);
_CRT_EXPORT IntStatus_t InterruptRestoreState(IntStatus_t state);

/* Helpers */
__CRT_EXTERN uint32_t InterruptGetPolarity(uint16_t IntiFlags, uint8_t IrqSource);
__CRT_EXTERN uint32_t InterruptGetTrigger(uint16_t IntiFlags, uint8_t IrqSource);

#endif //!_X86_INTERRUPTS_H_
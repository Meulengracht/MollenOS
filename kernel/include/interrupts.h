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
 * - Driver System */
#include <os/driver/driver.h>

/* Includes 
 * - System */
#include <os/ipc/ipc.h>
#include <os/osdefs.h>

/* Special flags that are available only
 * in kernel context for special interrupts */
#define INTERRUPT_KERNEL				0x10000000
#define INTERRUPT_SOFTWARE				0x20000000

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
__EXTERN void InterruptInitialize(void);

/* InterruptRegister
 * Tries to allocate the given interrupt source
 * by the given descriptor and flags. On success
 * it returns the id of the irq, and on failure it
 * returns UUID_INVALID */
__EXTERN UUId_t InterruptRegister(MCoreInterrupt_t *Interrupt, Flags_t Flags);

/* InterruptUnregister 
 * Unregisters the interrupt from the system and removes
 * any resources that was associated with that interrupt 
 * also masks the interrupt if it was the only user */
__EXTERN OsStatus_t InterruptUnregister(UUId_t Source);

/* InterruptAcknowledge 
 * Acknowledges the interrupt source and unmasks
 * the interrupt-line, allowing another interrupt
 * to occur for the given driver */
__EXTERN OsStatus_t InterruptAcknowledge(UUId_t Source);

/* InterruptGet
 * Retrieves the given interrupt source information
 * as a MCoreInterruptDescriptor_t */
__EXTERN MCoreInterruptDescriptor_t *InterruptGet(UUId_t Source);

/* InterruptDisable
 * Disables interrupts and returns
 * the state before disabling */
__EXTERN IntStatus_t InterruptDisable(void);

/* InterruptEnable
 * Enables interrupts and returns 
 * the state before enabling */
__EXTERN IntStatus_t InterruptEnable(void);

/* InterruptRestoreState
 * Restores the interrupt-status to the given
 * state, that must have been saved from SaveState */
__EXTERN IntStatus_t InterruptRestoreState(IntStatus_t State);

/* InterruptSaveState
 * Retrieves the current state of interrupts */
__EXTERN IntStatus_t InterruptSaveState(void);

/* InterruptIsDisabled
 * Returns 1 if interrupts are currently
 * disabled or 0 if interrupts are enabled */
__EXTERN int InterruptIsDisabled(void);

/* InterruptAllocateISA
 * Allocates the ISA interrupt source, if it's 
 * already allocated it returns OsError */
__EXTERN OsStatus_t InterruptAllocateISA(int Source);
__EXTERN Flags_t InterruptGetPolarity(uint16_t IntiFlags, int IrqSource);
__EXTERN Flags_t InterruptGetTrigger(uint16_t IntiFlags, int IrqSource);

/* AcpiDeriveInterrupt
 * Derives an interrupt by consulting
 * the bus of the device, and spits out flags in
 * AcpiConform and returns irq */
__EXTERN int AcpiDeriveInterrupt(DevInfo_t Bus,
	DevInfo_t Device, int Pin, Flags_t *AcpiConform);

/* InterruptDriver
 * Call this to send an interrupt into user-space
 * the driver must acknowledge the interrupt once its handled
 * to unmask the interrupt-line again */
__EXTERN
OsStatus_t
ScRpcExecute(
	_In_ MRemoteCall_t *Rpc, 
	_In_ UUId_t Target,
	_In_ int Async);

SERVICEAPI
OsStatus_t
SERVICEABI
InterruptDriver(
	_In_ UUId_t Ash, 
	_In_ UUId_t Id,
	_In_ void *Data)
{
	// Variables
	MRemoteCall_t Request;

	// Initialze RPC
	RPCInitialize(&Request, 1, PIPE_DEFAULT, __DRIVER_INTERRUPT);
	RPCSetArgument(&Request, 0, (__CONST void*)&Id, sizeof(UUId_t));
	RPCSetArgument(&Request, 1, (__CONST void*)Data, sizeof(void*));

	// Send
	return ScRpcExecute(&Request, Ash, 1);
}

#endif //!_MCORE_INTERRUPTS_H_

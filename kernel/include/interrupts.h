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

/* MCoreInterruptDescriptor
 * The kernel interrupt descriptor structure. Contains
 * all information neccessary to store registered interrupts. */
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
KERNELAPI
void
KERNELABI
InterruptInitialize(void);

/* InterruptRegister
 * Tries to allocate the given interrupt source
 * by the given descriptor and flags. On success
 * it returns the id of the irq, and on failure it
 * returns UUID_INVALID */
KERNELAPI
UUId_t
KERNELABI
InterruptRegister(
    _In_ MCoreInterrupt_t *Interrupt,
    _In_ Flags_t Flags);

/* InterruptUnregister 
 * Unregisters the interrupt from the system and removes
 * any resources that was associated with that interrupt 
 * also masks the interrupt if it was the only user */
KERNELAPI
OsStatus_t
KERNELABI
InterruptUnregister(
    _In_ UUId_t Source);

/* InterruptAcknowledge 
 * Acknowledges the interrupt source and unmasks
 * the interrupt-line, allowing another interrupt
 * to occur for the given driver */
KERNELAPI
OsStatus_t
KERNELABI
InterruptAcknowledge(
    _In_ UUId_t Source);

/* InterruptGet
 * Retrieves the given interrupt source information
 * as a MCoreInterruptDescriptor_t */
KERNELAPI
MCoreInterruptDescriptor_t*
KERNELABI
InterruptGet(
   _In_ UUId_t Source);

/* InterruptGetLeastLoaded
 * Allocates the least used sharable irq
 * most useful for MSI devices */
KERNELAPI
int
KERNELABI
InterruptGetLeastLoaded(
	_In_ int Irqs[],
	_In_ int Count);

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
KERNELAPI
int
KERNELABI
AcpiDeriveInterrupt(
    _In_ DevInfo_t Bus, 
    _In_ DevInfo_t Device,
    _In_ int Pin,
    _Out_ Flags_t *AcpiConform);

/* __KernelInterruptDriver
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
__KernelInterruptDriver(
	_In_ UUId_t Ash, 
	_In_ UUId_t Id,
	_In_ void *Data)
{
	// Variables
    MRemoteCall_t Request;
    size_t Zero = 0;

	// Initialze RPC
	RPCInitialize(&Request, 1, PIPE_RPCOUT, __DRIVER_INTERRUPT);
	RPCSetArgument(&Request, 0, (__CONST void*)&Id, sizeof(UUId_t));
    RPCSetArgument(&Request, 1, (__CONST void*)&Data, sizeof(void*));
    RPCSetArgument(&Request, 2, (__CONST void*)&Zero, sizeof(size_t));
    RPCSetArgument(&Request, 3, (__CONST void*)&Zero, sizeof(size_t));
    RPCSetArgument(&Request, 4, (__CONST void*)&Zero, sizeof(size_t));

	// Send
	return ScRpcExecute(&Request, Ash, 1);
}

/* __KernelTimeoutDriver
 * Call this to send an timeout into userspace. The driver is
 * then informed about a timer-interval that elapsed. */
SERVICEAPI
OsStatus_t
SERVICEABI
__KernelTimeoutDriver(
	_In_ UUId_t Ash, 
	_In_ UUId_t TimerId,
	_In_ void *TimerData)
{
	// Variables
    MRemoteCall_t Request;
    size_t Zero = 0;

	// Initialze RPC
	RPCInitialize(&Request, 1, PIPE_RPCOUT, __DRIVER_TIMEOUT);
	RPCSetArgument(&Request, 0, (__CONST void*)&TimerId, sizeof(UUId_t));
    RPCSetArgument(&Request, 1, (__CONST void*)&TimerData, sizeof(void*));
    RPCSetArgument(&Request, 2, (__CONST void*)&Zero, sizeof(size_t));
    RPCSetArgument(&Request, 3, (__CONST void*)&Zero, sizeof(size_t));
    RPCSetArgument(&Request, 4, (__CONST void*)&Zero, sizeof(size_t));

	// Send
	return ScRpcExecute(&Request, Ash, 1);
}

#endif //!_MCORE_INTERRUPTS_H_

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
#include <os/driver/interrupt.h>
#include <os/driver/driver.h>

/* Includes 
 * - System */
#include <os/ipc/ipc.h>
#include <os/osdefs.h>

/* Special flags that are available only
 * in kernel context for special interrupts */
#define INTERRUPT_KERNEL				0x10000000
#define INTERRUPT_CONTEXT               0x20000000

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
 * Initializes interrupt data-structures and global variables
 * by setting everything to sane value */
KERNELAPI
void
KERNELABI
InterruptInitialize(void);

/* InterruptStart
 * Starts the interrupt-queue thread and allocates resources
 * for the interrupt-queue pipes. */
KERNELAPI
void
KERNELABI
InterruptStart(void);

/* InterruptQueue
 * Queues a new interrupt for handling. If it was not
 * able to queue the interrupt it returns OsError */
KERNELAPI
OsStatus_t
KERNELABI
InterruptQueue(
    _In_ MCoreInterruptDescriptor_t *Interrupt);

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

/* InterruptGet
 * Retrieves the given interrupt source information
 * as a MCoreInterruptDescriptor_t */
KERNELAPI
MCoreInterruptDescriptor_t*
KERNELABI
InterruptGet(
   _In_ UUId_t Source);

/* InterruptGetIndex
 * Retrieves the given interrupt source information
 * as a MCoreInterruptDescriptor_t */
KERNELAPI
MCoreInterruptDescriptor_t*
KERNELABI
InterruptGetIndex(
   _In_ UUId_t TableIndex);

/* InterruptSetActiveStatus
 * Set's the current status for the calling cpu to
 * interrupt-active state */
KERNELAPI
void
KERNELABI
InterruptSetActiveStatus(
    _In_ int Active);

/* InterruptGetActiveStatus
 * Get's the current status for the calling cpu to
 * interrupt-active state */
KERNELAPI
int
KERNELABI
InterruptGetActiveStatus(void);

/* InterruptIncreasePenalty 
 * Increases the penalty for an interrupt source. */
KERNELAPI
OsStatus_t
KERNELABI
InterruptIncreasePenalty(
    _In_ int Source);

/* InterruptDecreasePenalty 
 * Decreases the penalty for an interrupt source. */
KERNELAPI
OsStatus_t
KERNELABI
InterruptDecreasePenalty(
    _In_ int Source);

/* InterruptGetPenalty
 * Retrieves the penalty for an interrupt source. 
 * If INTERRUPT_NONE is returned the source is unavailable. */
KERNELAPI
int
KERNELABI
InterruptGetPenalty(
    _In_ int Source);

/* InterruptGetLeastLoaded
 * Allocates the least used sharable irq
 * most useful for MSI devices */
KERNELAPI
int
KERNELABI
InterruptGetLeastLoaded(
	_In_ int Irqs[],
	_In_ int Count);

/* AcpiGetPolarityMode
 * Returns whether or not the polarity is Active Low or Active High.
 * For Active Low = 1, Active High = 0 */
KERNELAPI
int
KERNELABI
AcpiGetPolarityMode(
    _In_ uint16_t IntiFlags,
    _In_ int Source);

/* AcpiGetTriggerMode
 * Returns whether or not the trigger mode of the interrup is level or edge.
 * For Level = 1, Edge = 0 */
KERNELAPI
int
KERNELABI
AcpiGetTriggerMode(
    _In_ uint16_t IntiFlags,
    _In_ int Source);

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

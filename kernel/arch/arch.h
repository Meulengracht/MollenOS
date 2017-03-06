/* MollenOS
*
* Copyright 2011 - 2016, Philip Meulengracht
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
* MollenOS Architecture Header
*/

#ifndef _MCORE_MAIN_ARCH_
#define _MCORE_MAIN_ARCH_

/* Includes */
#include <os/osdefs.h>

/* Select Correct ARCH file */
#if defined(_X86_32)
#include "x86\x32\Arch.h"
#elif defined(_X86_64)
#include "x86\x64\Arch.h"
#else
#error "Unsupported Architecture :("
#endif

/* Typedef this */
typedef Registers_t Context_t;


/************************
 * Threading            *
 * Used for abstracting *
 * arch specific thread *
 ************************/
typedef struct _MCoreThread MCoreThread_t;

/* IThreadCreate
 * Initializes a new x86-specific thread context
 * for the given threading flags, also initializes
 * the yield interrupt handler first time its called */
__EXTERN void *IThreadCreate(Flags_t ThreadFlags, Addr_t EntryPoint);

/* IThreadSetupUserMode
 * Initializes user-mode data for the given thread, and
 * allocates all neccessary resources (x86 specific) for
 * usermode operations */
__EXTERN void IThreadSetupUserMode(MCoreThread_t *Thread, Addr_t StackAddress);

/* IThreadDestroy
 * Free's all the allocated resources for x86
 * specific threading operations */
__EXTERN void IThreadDestroy(MCoreThread_t *Thread);

/* IThreadWakeCpu
 * Wake's the target cpu from an idle thread
 * by sending it an yield IPI */
__EXTERN void IThreadWakeCpu(Cpu_t Cpu);

/* IThreadYield
 * Yields the current thread control to the scheduler */
__EXTERN void IThreadYield(void);

/************************
 * Device Io Spaces     *
 * Used for abstracting *
 * device addressing    *
 ************************/
#include <os/driver/io.h>

/* Represents an io-space in MollenOS, they represent
 * some kind of communication between hardware and software
 * by either port or mmio */
typedef struct _MCoreIoSpace {
	UUId_t				Id;
	UUId_t				Owner;
	int					Type;
	Addr_t				PhysicalBase;
	Addr_t				VirtualBase;
	size_t				Size;
} MCoreIoSpace_t;

/* Initialize the Io Space manager so we 
 * can register io-spaces from drivers and the
 * bus code */
__EXTERN void IoSpaceInitialize(void);

/* Registers an io-space with the io space manager 
 * and assigns the io-space a unique id for later
 * identification */
__EXTERN OsStatus_t IoSpaceRegister(DeviceIoSpace_t *IoSpace);

/* Acquires the given memory space by mapping it in
 * the current drivers memory space if needed, and sets
 * a lock on the io-space */
__EXTERN OsStatus_t IoSpaceAcquire(DeviceIoSpace_t *IoSpace);

/* Releases the given memory space by unmapping it from
 * the current drivers memory space if needed, and releases
 * the lock on the io-space */
__EXTERN OsStatus_t IoSpaceRelease(DeviceIoSpace_t *IoSpace);

/* Destroys the given io-space by its id, the id
 * has the be valid, and the target io-space HAS to 
 * un-acquired by any process, otherwise its not possible */
__EXTERN OsStatus_t IoSpaceDestroy(UUId_t IoSpace);

/* Tries to validate the given virtual address by 
 * checking if any process has an active io-space
 * that involves that virtual address */
__EXTERN Addr_t IoSpaceValidate(Addr_t Address);

/***********************
* Device Interface     *
***********************/
__EXTERN int DeviceAllocateInterrupt(void *mCoreDevice);

#endif
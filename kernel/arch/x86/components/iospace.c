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
 * MollenOS X86 IO Space Interface
 *
 */

/* Includes 
 * - System */
#include "../arch.h"
#include <process/server.h>
#include <memory.h>
#include <heap.h>
#include <log.h>

/* Includes
 * - Library */
#include <stddef.h>
#include <ds/list.h>

/* Globals 
 * We need to keep track of a few things
 * and keep a list of io-spaces in the system */
Spinlock_t __GlbIoSpaceLock = SPINLOCK_INIT;
IoSpaceId_t __GlbIoSpaceId = 0;
List_t *__GlbIoSpaces = NULL;
int __GlbIoSpaceInitialized = 0;

/* Initialize the Io Space manager so we 
 * can register io-spaces from drivers and the
 * bus code */
void IoSpaceInitialize(void)
{
	__GlbIoSpaces = ListCreate(KeyInteger, LIST_NORMAL);
	__GlbIoSpaceInitialized = 1;
	__GlbIoSpaceId = 0;
}

/* Registers an io-space with the io space manager 
 * and assigns the io-space a unique id for later
 * identification */
OsStatus_t IoSpaceRegister(DeviceIoSpace_t *IoSpace)
{
	/* Variables */
	MCoreIoSpace_t *SysCopy = NULL;
	DataKey_t Key;

	/* Before doing anything, we should do a over-lap
	 * check before trying to register this */

	/* Allocate a new system only copy of the io-space
	 * as we don't want anyone to edit our copy */
	SysCopy = (MCoreIoSpace_t*)kmalloc(sizeof(MCoreIoSpace_t));

	/* Acquire a lock on this as 
	 * we assign it an id */
	SpinlockAcquire(&__GlbIoSpaceLock);

	/* Initialize the system copy */
	IoSpace->Id = SysCopy->Id = __GlbIoSpaceId++;
	SysCopy->Owner = PHOENIX_NO_ASH;
	SysCopy->Type = IoSpace->Type;
	SysCopy->PhysicalBase = IoSpace->PhysicalBase;
	SysCopy->VirtualBase = 0;
	SysCopy->Size = IoSpace->Size;

	/* Release the lock again, its ok from here
	 * to allow other code to run */
	SpinlockRelease(&__GlbIoSpaceLock);

	/* Add to list */
	Key.Value = IoSpace->Id;
	ListAppend(__GlbIoSpaces, ListCreateNode(Key, Key, (void*)IoSpace));

	/* Done! */
	return OsNoError;
}

/* Acquires the given memory space by mapping it in
 * the current drivers memory space if needed, and sets
 * a lock on the io-space */
OsStatus_t IoSpaceAcquire(DeviceIoSpace_t *IoSpace)
{
	/* Variables we need for this 
	 * operation */
	MCoreServer_t *Server = PhoenixGetServer(SERVER_CURRENT);
	MCoreIoSpace_t *SysCopy = NULL;
	DataKey_t Key;
	Cpu_t Cpu;

	/* Lookup the system copy to validate this
	 * requested operation */
	Cpu = ApicGetCpu();
	Key.Value = (int)IoSpace->Id;
	SysCopy = (MCoreIoSpace_t*)ListGetDataByKey(__GlbIoSpaces, Key, 0);

	/* Sanitize the system copy */
	if (Server == NULL || SysCopy == NULL
		|| SysCopy->Owner != PHOENIX_NO_ASH) {
		return OsError;
	}

	/* Set owner! */
	SysCopy->Owner = ThreadingGetCurrentThread(Cpu)->AshId;

	/* Map it in (if the type equals mmio) */
	if (SysCopy->Type == IO_SPACE_MMIO) {
		int PageCount = DIVUP(SysCopy->Size, PAGE_SIZE);

		/* Do we cross a page boundary? */
		if (((SysCopy->PhysicalBase + SysCopy->Size) / PAGE_SIZE)
			!= (SysCopy->PhysicalBase / PAGE_SIZE)) {
			PageCount++;
		}

		/* Ok, so when we map it in and reserver space
		* for it, its important we set it with its offset */
		SysCopy->VirtualBase = IoSpace->VirtualBase = 
			BitmapAllocateAddress(Server->DriverMemory, PageCount * PAGE_SIZE)
				+ (SysCopy->PhysicalBase & ATTRIBUTE_MASK);
	}
	else if (SysCopy->Type == IO_SPACE_IO) {
		x86Thread_t *Tx = (x86Thread_t*)ThreadingGetCurrentThread(Cpu)->ThreadData;
		for (size_t i = 0; i < SysCopy->Size; i++) {
			TssEnableIo(Cpu, &Tx->IoMap[0],
				((uint16_t)(SysCopy->PhysicalBase + i)));
		}
	}

	/* Congrats, we did it! */
	return OsNoError;
}

/* Releases the given memory space by unmapping it from
 * the current drivers memory space if needed, and releases
 * the lock on the io-space */
OsStatus_t IoSpaceRelease(DeviceIoSpace_t *IoSpace)
{
	/* Variables we need for this 
	 * operation */
	MCoreServer_t *Server = PhoenixGetServer(SERVER_CURRENT);
	MCoreIoSpace_t *SysCopy = NULL;
	DataKey_t Key;
	Cpu_t Cpu;

	/* Lookup the system copy to validate this
	 * requested operation */
	Cpu = ApicGetCpu();
	Key.Value = (int)IoSpace->Id;
	SysCopy = (MCoreIoSpace_t*)ListGetDataByKey(__GlbIoSpaces, Key, 0);

	/* Sanitize the system copy and do
	 * some security checks */
	if (Server == NULL || SysCopy == NULL
		|| SysCopy->Owner != Server->Base.Id) {
		return OsError;
	}

	/* Make sure we unmap all resources */
	if (SysCopy->Type == IO_SPACE_MMIO) {
		int PageCount = DIVUP(SysCopy->Size, PAGE_SIZE);

		/* Do we cross a page boundary? */
		if (((SysCopy->PhysicalBase + SysCopy->Size) / PAGE_SIZE)
			!= (SysCopy->PhysicalBase / PAGE_SIZE)) {
			PageCount++;
		}

		/* Unmap them */
		BitmapFreeAddress(Server->DriverMemory, SysCopy->VirtualBase,
			PageCount * PAGE_SIZE);
	}
	else if (SysCopy->Type == IO_SPACE_IO) {
		x86Thread_t *Tx = (x86Thread_t*)ThreadingGetCurrentThread(Cpu)->ThreadData;
		for (size_t i = 0; i < SysCopy->Size; i++) {
			TssDisableIo(Cpu, &Tx->IoMap[0],
				((uint16_t)(SysCopy->PhysicalBase + i)));
		}
	}

	/* Clear out some stuff */
	SysCopy->VirtualBase = IoSpace->VirtualBase = 0;
	SysCopy->Owner = PHOENIX_NO_ASH;

	/* Done! */
	return OsNoError;
}

/* Destroys the given io-space by its id, the id
 * has the be valid, and the target io-space HAS to 
 * un-acquired by any process, otherwise its not possible */
OsStatus_t IoSpaceDestroy(IoSpaceId_t IoSpace)
{
	/* Variables we need for this
	 * operation */
	MCoreIoSpace_t *SysCopy = NULL;
	DataKey_t Key;

	/* Lookup the system copy to validate this
	 * requested operation */
	Key.Value = (int)IoSpace;
	SysCopy = (MCoreIoSpace_t*)ListGetDataByKey(__GlbIoSpaces, Key, 0);

	/* Sanitize the system copy */
	if (SysCopy == NULL
		|| SysCopy->Owner != PHOENIX_NO_ASH) {
		return OsError;
	}

	/* Remove from list and cleanup 
	 * the allocated resources */
	ListRemoveByKey(__GlbIoSpaces, Key);
	kfree(SysCopy);

	/* Done! */
	return OsNoError;
}

/* Tries to validate the given virtual address by 
 * checking if any process has an active io-space
 * that involves that virtual address */
Addr_t IoSpaceValidate(Addr_t Address)
{
	/* Ok, first of all, we need to validate that
	 * it's actually a process trying to do this */
	PhxId_t ProcessId = ThreadingGetCurrentThread(ApicGetCpu())->AshId;

	/* Sanitize the id */
	if (ProcessId == PHOENIX_NO_ASH) {
		return 0;
	}

	/* Iterate and check each io-space
	 * if anyone has this mapped in */
	foreach(ioNode, __GlbIoSpaces) {
		MCoreIoSpace_t *IoSpace =
			(MCoreIoSpace_t*)ioNode->Data;

		/* Two things has to be true before the io-space
		 * is valid, it has to belong to the right process
		 * and be in range */
		if (IoSpace->Owner == ProcessId
			&& (Address >= IoSpace->VirtualBase
				&& Address < (IoSpace->VirtualBase + IoSpace->Size))) {
			return IoSpace->PhysicalBase + (Address - IoSpace->VirtualBase);
		}
	}

	/* Damn */
	return 0;
}

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
* MollenOS X86 IO Space Interface
*
*/

/* Includes */
#include "../Arch.h"
#include <Memory.h>
#include <Pci.h>
#include <Heap.h>
#include <Log.h>

/* C-Library */
#include <stddef.h>
#include <ds/list.h>

/* Globals */
List_t *GlbIoSpaces = NULL;
int GlbIoSpaceInitialized = 0;
int GlbIoSpaceId = 0;

/* Init Io Spaces */
void IoSpaceInit(void)
{
	/* Create list */
	GlbIoSpaces = ListCreate(KeyInteger, LIST_NORMAL);
	GlbIoSpaceInitialized = 1;
	GlbIoSpaceId = 0;
}

/* Device Io Space */
MCoreIoSpace_t *IoSpaceCreate(int Type, Addr_t PhysicalBase, size_t Size)
{
	/* Allocate */
	MCoreIoSpace_t *IoSpace = (MCoreIoSpace_t*)kmalloc(sizeof(MCoreIoSpace_t));
	DataKey_t Key;

	/* Setup */
	IoSpace->Id = GlbIoSpaceId;
	GlbIoSpaceId++;
	IoSpace->Type = Type;
	IoSpace->PhysicalBase = PhysicalBase;
	IoSpace->VirtualBase = 0;
	IoSpace->Size = Size;

	/* Map it in (if needed) */
	if (Type == DEVICE_IO_SPACE_MMIO)
	{
		/* Calculate number of pages to map in */
		int PageCount = Size / PAGE_SIZE;
		if (Size % PAGE_SIZE)
			PageCount++;

		/* Do we cross a page boundary? */
		if (((PhysicalBase + Size) / PAGE_SIZE)
			!= (PhysicalBase / PAGE_SIZE))
			PageCount++;

		/* Map it WITH the page offset */
		IoSpace->VirtualBase =
			((Addr_t)MmReserveMemory(PageCount)
			+ (PhysicalBase & ATTRIBUTE_MASK));
	}

	/* Add to list */
	Key.Value = IoSpace->Id;
	ListAppend(GlbIoSpaces, ListCreateNode(Key, Key, (void*)IoSpace));

	/* Done! */
	return IoSpace;
}

/* Cleanup Io Space */
void IoSpaceDestroy(MCoreIoSpace_t *IoSpace)
{
	/* DataKey for list */
	DataKey_t Key;

	/* Sanity */
	if (IoSpace->Type == DEVICE_IO_SPACE_MMIO)
	{
		/* Calculate number of pages to ummap */
		int i, PageCount = IoSpace->Size / PAGE_SIZE;
		if (IoSpace->Size % PAGE_SIZE)
			PageCount++;

		/* Unmap them */
		for (i = 0; i < PageCount; i++)
			MmVirtualUnmap(NULL, IoSpace->VirtualBase + (i * PAGE_SIZE));
	}

	/* Remove from list */
	Key.Value = IoSpace->Id;
	ListRemoveByKey(GlbIoSpaces, Key);

	/* Free */
	kfree(IoSpace);
}

/* Read from device space */
size_t IoSpaceRead(MCoreIoSpace_t *IoSpace, size_t Offset, size_t Length)
{
	/* Result */
	size_t Result = 0;

	/* Sanity */
	if ((Offset + Length) > IoSpace->Size) {
		LogFatal("SYST", "Invalid access to resource, %u exceeds the allocated io-space", (Offset + Length));
		return 0;
	}

	/* Sanity */
	if (IoSpace->Type == DEVICE_IO_SPACE_IO)
	{
		/* Calculate final address */
		uint16_t IoPort = (uint16_t)IoSpace->PhysicalBase + (uint16_t)Offset;

		switch (Length) {
		case 1:
			Result = inb(IoPort);
			break;
		case 2:
			Result = inw(IoPort);
			break;
		case 4:
			Result = inl(IoPort);
			break;
		default:
			break;
		}
	}
	else if (IoSpace->Type == DEVICE_IO_SPACE_MMIO)
	{
		/* Calculat final address */
		Addr_t MmAddr = IoSpace->VirtualBase + Offset;

		switch (Length) {
		case 1:
			Result = *(uint8_t*)MmAddr;
			break;
		case 2:
			Result = *(uint16_t*)MmAddr;
			break;
		case 4:
			Result = *(uint32_t*)MmAddr;
			break;
#ifdef _X86_64
		case 8:
			Result = *(uint64_t*)MmAddr;
			break;
#endif
		default:
			break;
		}
	}

	/* Done! */
	return Result;
}

/* Write to device space */
void IoSpaceWrite(MCoreIoSpace_t *IoSpace, size_t Offset, size_t Value, size_t Length)
{
	/* Sanity */
	if ((Offset + Length) > IoSpace->Size) {
		LogFatal("SYST", "Invalid access to resource, %u exceeds the allocated io-space", (Offset + Length));
		return;
	}

	/* Sanity */
	if (IoSpace->Type == DEVICE_IO_SPACE_IO)
	{
		/* Calculate final address */
		uint16_t IoPort = (uint16_t)IoSpace->PhysicalBase + (uint16_t)Offset;

		switch (Length) {
		case 1:
			outb(IoPort, (uint8_t)(Value & 0xFF));
			break;
		case 2:
			outw(IoPort, (uint16_t)(Value & 0xFFFF));
			break;
		case 4:
			outl(IoPort, (uint32_t)(Value & 0xFFFFFFFF));
			break;
		default:
			break;
		}
	}
	else if (IoSpace->Type == DEVICE_IO_SPACE_MMIO)
	{
		/* Calculat final address */
		Addr_t MmAddr = IoSpace->VirtualBase + Offset;

		switch (Length) {
		case 1:
			*(uint8_t*)MmAddr = (uint8_t)(Value & 0xFF);
			break;
		case 2:
			*(uint16_t*)MmAddr = (uint16_t)(Value & 0xFFFF);
			break;
		case 4:
			*(uint32_t*)MmAddr = (uint32_t)(Value & 0xFFFFFFFF);
			break;
#ifdef _X86_64
		case 8:
			*(uint64_t*)MmAddr = (uint64_t)(Value & 0xFFFFFFFFFFFFFFFF);
			break;
#endif
		default:
			break;
		}
	}
}

/* Validate Address */
Addr_t IoSpaceValidate(Addr_t Address)
{
	/* Iterate and check */
	foreach(ioNode, GlbIoSpaces)
	{
		/* Cast */
		MCoreIoSpace_t *IoSpace =
			(MCoreIoSpace_t*)ioNode->Data;

		/* Let's see */
		if (Address >= IoSpace->VirtualBase
			&& Address < (IoSpace->VirtualBase + IoSpace->Size)) {
			/* Calc offset page */
			Addr_t Offset = (Address - IoSpace->VirtualBase);
			return IoSpace->PhysicalBase + Offset;
		}
	}

	/* Damn */
	return 0;
}

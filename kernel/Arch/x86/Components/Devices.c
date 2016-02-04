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
* MollenOS X86 Devices Interfaces
*
*/

/* Includes */
#include <DeviceManager.h>
#include <Modules/ModuleManager.h>
#include "../Arch.h"
#include <Memory.h>
#include <AcpiSys.h>
#include <Pci.h>
#include <Heap.h>
#include <List.h>
#include <Timers.h>
#include <Log.h>

/* Definitions */
#define DEVICES_CMOS			0x00000000
#define DEVICES_PS2				0x00000010
#define DEVICES_PIT				0x00000018
#define DEVICES_RTC				0x00000020

/* Initialises all available timers in system */
void DevicesInitTimers(void)
{
	/* Vars */
	MCoreModule_t *Module = NULL;
	ACPI_TABLE_HEADER *Header = NULL;

	/* Information */
	LogInformation("TIMR", "Initializing System Timers");

	/* Step 1. Load the CMOS Clock */
	Module = ModuleFindGeneric(DEVICEMANAGER_LEGACY_CLASS, DEVICES_CMOS);

	/* Do we have the driver? */
	if (Module != NULL)
		ModuleLoad(Module, &AcpiGbl_FADT.Century);

	/* Step 2. Try to setup HPET 
	 * I'd rather ignore the rest */
	if (ACPI_SUCCESS(AcpiGetTable(ACPI_SIG_HPET, 0, &Header)))
	{
		/* There is hope, we have Hpet header */
		Module = ModuleFindGeneric(DEVICEMANAGER_LEGACY_CLASS, DEVICEMANAGER_ACPI_HPET);

		/* Do we have the driver? */
		if (Module != NULL)
		{
			/* Cross fingers for the Hpet driver */
			if (ModuleLoad(Module, (void*)Header) == ModuleOk)
				return;
		}
	}

	/* Damn.. 
	 * Step 3. Initialize the PIT */
	Module = ModuleFindGeneric(DEVICEMANAGER_LEGACY_CLASS, DEVICES_PIT);

	/* Do we have the driver? */
	if (Module != NULL)
	{
		/* Great, load driver */
		if (ModuleLoad(Module, NULL) == ModuleOk)
			return;
	}
		
	/* Wtf? No PIT? 
	 * Step 4. Last resort to the rtc-clock */
	Module = ModuleFindGeneric(DEVICEMANAGER_LEGACY_CLASS, DEVICES_RTC);

	/* Do we have the driver? */
	if (Module != NULL)
		ModuleLoad(Module, NULL);

	/* PS2 */
	Module = ModuleFindGeneric(DEVICEMANAGER_LEGACY_CLASS, DEVICES_PS2);

	/* Do we have the driver? */
	if (Module != NULL)
		ModuleLoad(Module, NULL);
}

/* Globals */
list_t *GlbIoSpaces = NULL;
int GlbIoSpaceInitialized = 0;
int GlbIoSpaceId = 0;

/* Externs */
extern x86CpuObject_t GlbBootCpuInfo;

/* Init Io Spaces */
void IoSpaceInit(void)
{
	/* Create list */
	GlbIoSpaces = list_create(LIST_NORMAL);
	GlbIoSpaceInitialized = 1;
	GlbIoSpaceId = 0;
}

/* Device Io Space */
DeviceIoSpace_t *IoSpaceCreate(int Type, Addr_t PhysicalBase, size_t Size)
{
	/* Allocate */
	DeviceIoSpace_t *IoSpace = (DeviceIoSpace_t*)kmalloc(sizeof(DeviceIoSpace_t));

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

		/* Map it */
		IoSpace->VirtualBase = (Addr_t)MmReserveMemory(PageCount);
	}

	/* Add to list */
	list_append(GlbIoSpaces, 
		list_create_node(IoSpace->Id, (void*)IoSpace));
	
	/* Done! */
	return IoSpace;
}

/* Cleanup Io Space */
void IoSpaceDestroy(DeviceIoSpace_t *IoSpace)
{
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
	list_remove_by_id(GlbIoSpaces, IoSpace->Id);

	/* Free */
	kfree(IoSpace);
}

/* Read from device space */
size_t IoSpaceRead(DeviceIoSpace_t *IoSpace, size_t Offset, size_t Length)
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
void IoSpaceWrite(DeviceIoSpace_t *IoSpace, size_t Offset, size_t Value, size_t Length)
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
		DeviceIoSpace_t *IoSpace = 
			(DeviceIoSpace_t*)ioNode->data;

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

/* Backup Timer, Should always be provided */
extern void rdtsc(uint64_t *Value);

void DelayMs(uint32_t MilliSeconds)
{
	/* Keep value in this */
	uint64_t Counter = 0;
	volatile uint64_t TimeOut = 0;

	/* Sanity */
	if (!(GlbBootCpuInfo.EdxFeatures & CPUID_FEAT_EDX_TSC))
	{
		LogFatal("TIMR", "DelayMs() was called, but no TSC support in CPU.");
		Idle();
	}

	/* Use rdtsc for this */
	rdtsc(&Counter);
	TimeOut = Counter + (uint64_t)(MilliSeconds * 100000);

	while (Counter < TimeOut) { rdtsc(&Counter); }
}
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
* MollenOS Common Entry Point
*/

/* Includes */
#include <revision.h>
#include <MollenOS.h>
#include <Arch.h>
#include <Devices/Cpu.h>
#include <Devices/Video.h>
#include <DeviceManager.h>
#include <Scheduler.h>
#include <Threading.h>
#include <Heap.h>
#include <stdio.h>

/* We need these functions */
extern void ThreadingDebugPrint(void);

/* Print Header Information */
void PrintHeader(MCoreBootInfo_t *BootInfo)
{
	printf("MollenOS Operating System - Platform: %s - Version %i.%i.%i\n",
		ARCHITECTURE_NAME, REVISION_MAJOR, REVISION_MINOR, REVISION_BUILD);
	printf("Written by Philip Meulengracht, Copyright 2011-2014, All Rights Reserved.\n");
	printf("Bootloader - %s\n", BootInfo->BootloaderName);
	printf("VC Build %s - %s\n\n", BUILD_DATE, BUILD_TIME);
}

/* Shared Entry in MollenOS */
void MCoreInitialize(MCoreBootInfo_t *BootInfo)
{
	/* We'll need these untill dynamic memory */
	MCoreCpuDevice_t BootCpu;
	MCoreVideoDevice_t BootVideo;

	/* Initialize Cpu */
	CpuInit(&BootCpu, BootInfo->ArchBootInfo);

	/* Setup Video Boot */
	VideoInit(&BootVideo, BootInfo);

	/* Print Header */
	PrintHeader(BootInfo);

	/* Init HAL */
	printf("  - Setting up base HAL\n");
	BootInfo->InitHAL(BootInfo->ArchBootInfo);

	/* Init the heap */
	HeapInit();

	/* Init post-heap systems */
	DmInit();
	DmCreateDevice("Processor", DeviceCpu, &BootCpu);
	DmCreateDevice("BootVideo", DeviceVideo, &BootVideo);

	/* Init Threading & Scheduler for boot cpu */
	printf("  - Threading\n");
	SchedulerInit(0);
	ThreadingInit();

	/* Init post-systems */
	printf("  - Initializing Post Memory Systems\n");
	BootInfo->InitPostSystems();

	/* Start out any extra cores */
	printf("  - Booting Cores\n");
	_SmpSetup();

	/* Virtual Filesystem */

	/* From this point, we should start seperate threads and
	* let this thread die out, because initial system setup
	* is now totally done, and the moment we start another
	* thread, it will take over as this is the idle thread */

	/* Drivers */
	//printf("  - Initializing Drivers...\n");
	//ThreadingCreateThread("DriverSetup", DriverManagerInit, NULL, 0);

	/* Start the compositor */
	//ThreadingDebugPrint();
}
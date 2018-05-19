/* MollenOS
 *
 * Copyright 2011, Philip Meulengracht
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
 * MollenOS x86 - Symmetrical Multiprocessoring
 *  - Contains the implementation of booting and initializing the other
 *    cpu cores in the system if any is present
 */
#define __MODULE "SMP0"
#define __TRACE

/* Includes 
 * - System */
#include <system/interrupts.h>
#include <system/utils.h>
#include <acpi.h>
#include <apic.h>
#include <gdt.h>
#include <thread.h>
#include <scheduler.h>
#include <interrupts.h>
#include <memory.h>
#include <debug.h>
#include <idt.h>
#include <cpu.h>
#include <string.h>
#include <stdio.h>
#include <ds/collection.h>

/* Includes
 * - Components */
#include <component/domain.h>
#include <component/cpu.h>

/* Externs */
extern const char *__GlbTramplineCode;
extern const int __GlbTramplineCode_length;
extern Collection_t *GlbAcpiNodes;
extern UUId_t GlbBootstrapCpuId;
volatile int GlbCpusBooted = 1;

/* Entry for AP Cores */
void SmpApEntry(void)
{
    // Variables
	UUId_t Cpu;

	// Disable interrupts and setup descriptors
	InterruptDisable();
	GdtInstall();
	IdtInstall();

	// Initialize CPU
	CpuInitialize();
	Cpu = (ApicReadLocal(APIC_PROCESSOR_ID) >> 24) & 0xFF;

	// Initialize Memory
	MmVirtualInstallPaging(Cpu);
	ApicInitAp();

    // Install the TSS before any multitasking
	GdtInstallTss(Cpu, 0);
#if defined(amd64) || defined(__amd64__)
    TssCreateStacks(Cpu);
#endif
    InterruptEnable();

    // Register with system - no returning
    ActivateApplicationCore(GetCurrentProcessorCore());
}

/* SmpBootCore
 * Handles the booting of the given cpu core. It performs the sequence IPI-SIPI(-SIPI)
 * and checks if the core booted. Otherwise it moves on to the next */
void
SmpBootCore(
    _In_ void*  Data,
    _In_ int    n,
    _In_ void*  UserData)
{
	// Variables
	ACPI_MADT_LOCAL_APIC *Core  = (ACPI_MADT_LOCAL_APIC*)Data;
	volatile int TargetCpuCount = GlbCpusBooted + 1;

	// Dont boot bootstrap cpu or if we already have 8 cores running
	if (GlbBootstrapCpuId == Core->Id || GlbCpusBooted > 7) {
        return;
    }
    
	// Perform the IPI
	TRACE(" > Booting core %u", Core->Id);
	if (ApicPerformIPI(Core->Id) != OsSuccess) {
		ERROR("Failed to boot core %u (IPI failed)", Core->Id);
		return;
	}

    // Perform the SIPI - some cpu's require two SIPI's
	if (ApicPerformSIPI(Core->Id, MEMORY_LOCATION_TRAMPOLINE_CODE) != OsSuccess) {
		ERROR("Failed to boot core %u (SIPI failed)", Core->Id);
		return;
    }

    // Stall - check if it booted, give it 200ms
    // If it didn't boot then send another SIPI and give up
    CpuStall(200);
    if (GlbCpusBooted != TargetCpuCount) {
        if (ApicPerformSIPI(Core->Id, MEMORY_LOCATION_TRAMPOLINE_CODE) != OsSuccess) {
            ERROR("Failed to boot core %u (SIPI failed)", Core->Id);
            return;
        }
    }
}

/* CpuSmpInitialize
 * Initializes an SMP environment and boots the
 * available cores in the system */
void
CpuSmpInitialize(void)
{
    // Variables
    uint32_t *CodePointer   = (uint32_t*)(__GlbTramplineCode + __GlbTramplineCode_length); 
	uint32_t EntryCode      = (uint32_t)(uint32_t*)SmpApEntry;
	DataKey_t Key;

    // Debug
    TRACE("CpuSmpInitialize()");

    // Initialize variables
    *(CodePointer - 1) = EntryCode;
    *(CodePointer - 2) = AddressSpaceGetCurrent()->Data[ASPACE_DATA_CR3];

    // Initialize the trampoline code in memory
	memcpy((void*)MEMORY_LOCATION_TRAMPOLINE_CODE, (char*)__GlbTramplineCode, __GlbTramplineCode_length);
	
    // Boot all cores
    Key.Value = ACPI_MADT_TYPE_LOCAL_APIC;
	CollectionExecuteOnKey(GlbAcpiNodes, SmpBootCore, Key, NULL);
}

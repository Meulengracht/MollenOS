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

#include <system/interrupts.h>
#include <system/utils.h>
#include <memoryspace.h>
#include <machine.h>
#include <memory.h>
#include <debug.h>
#include <apic.h>
#include <gdt.h>
#include <idt.h>
#include <cpu.h>
#include <string.h>
#include <component/domain.h>
#include <component/cpu.h>

/* External boot-stage for the application cores to jump into
 * kernel code and get initialized for execution. */
extern const int    __GlbTramplineCode_length;
extern const char   __GlbTramplineCode[];

/* SmpApplicationCoreEntry
 * The entry point for other cpu cores to get ready for code execution. This is neccessary
 * as the cores boot in 16 bit mode (64 bit for EFI?). The state we expect the core in is that
 * it has paging enabled in the same address space as boot-core. */
void
SmpApplicationCoreEntry(void)
{
	// Disable interrupts and setup descriptors
	InterruptDisable();
	CpuInitializeFeatures();
	GdtInstall();
	IdtInstall();

    // Switch into NUMA memory space if any, otherwise nothing happens
    SwitchSystemMemorySpace(GetCurrentSystemMemorySpace());
	InitializeLocalApicForApplicationCore();

    // Install the TSS before any multitasking
	TssInitialize(0);

    // Register with system - no returning
    ActivateApplicationCore(GetCurrentProcessorCore());
}

/* StartApplicationCore (@arch)
 * Initializes and starts the cpu core given. This is called by the kernel if it detects multiple
 * cores in the processor. */
void
StartApplicationCore(
    _In_ SystemCpuCore_t*   Core)
{
	// Perform the IPI
	TRACE(" > Booting core %u", Core->Id);
	if (ApicPerformIPI(Core->Id, 1) != OsSuccess) {
		ERROR("Failed to boot core %u (IPI failed)", Core->Id);
		return;
	}
    // ApicPerformIPI(Core->Id, 0); is needed on older cpus

    // Perform the SIPI - some cpu's require two SIPI's
	if (ApicPerformSIPI(Core->Id, MEMORY_LOCATION_TRAMPOLINE_CODE) != OsSuccess) {
		ERROR("Failed to boot core %u (SIPI failed)", Core->Id);
		return;
    }

    // Wait - check if it booted, give it 200ms
    // If it didn't boot then send another SIPI and give up
    CpuStall(200);
    if (Core->State != CpuStateRunning) {
        if (ApicPerformSIPI(Core->Id, MEMORY_LOCATION_TRAMPOLINE_CODE) != OsSuccess) {
            ERROR("Failed to boot core %u (SIPI failed)", Core->Id);
            return;
        }
    }
}

/* CpuSmpInitialize
 * Initializes an SMP environment and boots the available cores in the system */
void
CpuSmpInitialize(void)
{
    uint32_t *CodePointer   = (uint32_t*)((uint8_t*)(&__GlbTramplineCode[0]) + __GlbTramplineCode_length); 
	uint32_t EntryCode      = (uint32_t)(uint32_t*)SmpApplicationCoreEntry;

    TRACE("CpuSmpInitialize(%i)", GetMachine()->Processor.NumberOfCores);

    *(CodePointer - 1) = EntryCode;
    *(CodePointer - 2) = GetCurrentSystemMemorySpace()->Data[MEMORY_SPACE_CR3];
	memcpy((void*)MEMORY_LOCATION_TRAMPOLINE_CODE, (char*)__GlbTramplineCode, __GlbTramplineCode_length);
}

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
 * MollenOS x86 Initialization code
 * - Handles setup from a x86 entry point
 */

/* Includes 
 * - System */
#include <system/setup.h>
#include <system/utils.h>
#include <multiboot.h>
#include <interrupts.h>
#include <memory.h>
#include <arch.h>
#include <apic.h>
#include <cpu.h>
#include <gdt.h>
#include <idt.h>
#include <pic.h>
#include <log.h>
#include <vbe.h>

/* SystemInformationQuery 
 * Queries information about the running system
 * and the underlying architecture */
OsStatus_t
SystemInformationQuery(
	_Out_ SystemInformation_t *Information)
{
	// Copy memory information
	if (MmPhysicalQuery(&Information->PagesTotal, 
		&Information->PagesAllocated) != OsSuccess) {
		return OsError;
	}

	// Done
	return OsSuccess;
}

/* SystemFeaturesQuery
 * Called by the kernel to get which systems we support */
OsStatus_t
SystemFeaturesQuery(
    _In_ Multiboot_t *BootInformation,
    _Out_ Flags_t *SystemsSupported)
{
    // Variables
    Flags_t Features = 0;

    // Of course we support software features
    Features |= SYSTEM_FEATURE_INITIALIZE;
    Features |= SYSTEM_FEATURE_FINALIZE;

    // Memory features
    Features |= SYSTEM_FEATURE_MEMORY;
    Features |= SYSTEM_FEATURE_ADDRESSPACES;

    // Output features
    Features |= SYSTEM_FEATURE_OUTPUT;

    // Hardware features
    Features |= SYSTEM_FEATURE_INTERRUPTS;

    // Done
    *SystemsSupported = Features;
    return OsSuccess;
}

/* SystemFeaturesInitialize
 * Called by the kernel to initialize a supported system */
OsStatus_t
SystemFeaturesInitialize(
    _In_ Multiboot_t *BootInformation,
    _In_ Flags_t Systems)
{
    // Handle the system initialization, this should only
    // handle things that have absoultely no dependences at all
    if (Systems & SYSTEM_FEATURE_INITIALIZE) {
        CpuInitialize();
        GdtInitialize();
        IdtInitialize();
        PicInitialize();
    }

    // Handle the output initialization
    if (Systems & SYSTEM_FEATURE_OUTPUT) {
        VbeInitialize(BootInformation);
    }

    // Handle the memory initialization
    if (Systems & SYSTEM_FEATURE_MEMORY) {
        MmPhyiscalInit(BootInformation);
        MmVirtualInit();
    }

    // Handle interrupt initialization
    if (Systems & SYSTEM_FEATURE_INTERRUPTS) {
        if (CpuHasFeatures(0, CPUID_FEAT_EDX_APIC) != OsSuccess) {
            return OsError;
        }

        // Make sure we allocate all device interrupts
        // so system can't take control of them
        InterruptIncreasePenalty(0); // PIT
        InterruptIncreasePenalty(1); // PS/2
        InterruptIncreasePenalty(2); // PIT / Cascade
        InterruptIncreasePenalty(3); // COM 2/4
        InterruptIncreasePenalty(4); // COM 1/3
        InterruptIncreasePenalty(5); // LPT2
        InterruptIncreasePenalty(6); // Floppy
        InterruptIncreasePenalty(7); // LPT1 / Spurious
        InterruptIncreasePenalty(8); // CMOS
        InterruptIncreasePenalty(12); // PS/2
        InterruptIncreasePenalty(13); // FPU
        InterruptIncreasePenalty(14); // IDE
        InterruptIncreasePenalty(15); // IDE / Spurious

        ApicInitBoot();
        //CpuSmpInit(); -- Disable till further notice, we need a fix for stall
    }

    // Handle final things before the system spawns
    // all services and drivers
    if (Systems & SYSTEM_FEATURE_FINALIZE) {
        // Free all the allocated isa's now for drivers
        InterruptDecreasePenalty(0); // PIT
        InterruptDecreasePenalty(1); // PS/2
        InterruptDecreasePenalty(2); // PIT / Cascade
        InterruptDecreasePenalty(3); // COM 2/4
        InterruptDecreasePenalty(4); // COM 1/3
        InterruptDecreasePenalty(5); // LPT2
        InterruptDecreasePenalty(6); // Floppy
        InterruptDecreasePenalty(7); // LPT1 / Spurious
        InterruptDecreasePenalty(8); // CMOS
        InterruptDecreasePenalty(12); // PS/2
        InterruptDecreasePenalty(13); // FPU
        InterruptDecreasePenalty(14); // IDE
        InterruptDecreasePenalty(15); // IDE
    }

    // Done
    return OsSuccess;
}

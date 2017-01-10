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
#include <mollenos.h>
#include <arch.h>
#include <multiboot.h>
#include <memory.h>
#include <gdt.h>
#include <idt.h>
#include <interrupts.h>
#include <apic.h>
#include <log.h>

/* Variables that we can share with our project
 * primarily just boot information in case */
MCoreBootInfo_t x86BootInfo;

/* Extern, this function is declared in the MCore project
 * and all platform libs should enter this function */
__CRT_EXTERN void MCoreInitialize(MCoreBootInfo_t*);

/* Stuff from our own project that we import
 * we need some information about the cpu config */
__CRT_EXTERN x86CpuObject_t GlbBootCpuInfo;

/* Initializes the local apic (if present, it faults
 * in case it's not, we don't support less for now)
 * and it boots all the dormant cores (if any) */
void BootInitializeApic(void)
{
	/* Initialize the APIC (if present) */
	if (!(GlbBootCpuInfo.EdxFeatures & CPUID_FEAT_EDX_APIC)) {
		LogFatal("APIC", "BootInitializeApic::NOT PRESENT!");
		Idle();
	}
	else {
		LogInformation("APIC", "Initializing local interrupt chip");
	}

	/* Init the apic on the boot 
	 * processor, then try to boot cpus */
	ApicInitBoot();
	//CpuSmpInit(); -- Disable till further notice, we need a fix for stall
}

/* This initializes all the basic parts of the HAL
 * and includes basic interrupt setup, memory setup and
 * basic serial/video output */
void HALInit(void *BootInfo, MCoreBootDescriptor *Descriptor)
{
	/* Initialize output */
	VideoInit(BootInfo);

	/* Print */
	LogInformation("HAL0", "Initializing hardware layer");

	/* Setup x86 descriptor-tables
	 * which needs to happen on both 32/64 bit */
	GdtInitialize();
	IdtInit();

	/* Install all the interrupt handlers, we 
	 * install ALL 255 handlers, since we use a common
	 * entry point */
	InterruptInit();

	/* Memory setup! */
	LogInformation("HAL0", "Initializing physical and virtual memory");
	MmPhyiscalInit(BootInfo, Descriptor);
	MmVirtualInit();
}

/* This entry point is valid for both 32 and 64 bit
 * so this is where we do any mutual setup before 
 * calling the mcore entry */
void InitX86(Multiboot_t *BootInfo, MCoreBootDescriptor *BootDescriptor)
{
	/* Setup Boot Info */
	x86BootInfo.ArchBootInfo = (void*)BootInfo;
	x86BootInfo.BootloaderName = (char*)BootInfo->BootLoaderName;
	
	/* Setup Kern & Mod info */
	x86BootInfo.Descriptor.KernelAddress = BootDescriptor->KernelAddress;
	x86BootInfo.Descriptor.KernelSize = BootDescriptor->KernelSize;

	x86BootInfo.Descriptor.RamDiskAddress = BootDescriptor->RamDiskAddress;
	x86BootInfo.Descriptor.RamDiskSize = BootDescriptor->RamDiskSize;

	x86BootInfo.Descriptor.ExportsAddress = BootDescriptor->ExportsAddress;
	x86BootInfo.Descriptor.ExportsSize = BootDescriptor->ExportsSize;

	x86BootInfo.Descriptor.SymbolsAddress = BootDescriptor->SymbolsAddress;
	x86BootInfo.Descriptor.SymbolsSize = BootDescriptor->SymbolsSize;
	
	/* Setup Functions */
	x86BootInfo.InitHAL = HALInit;
	x86BootInfo.InitPostSystems = BootInitializeApic;

	/* Initialize Cpu */
	CpuInit();

	/* Call Entry Point */
	MCoreInitialize(&x86BootInfo);
}

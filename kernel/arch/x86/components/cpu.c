/**
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
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * x86 Cpu Information Header
 * - Contains some definitions and structures for helping around
 *   in the sub-layer system
 */
#define __MODULE "CCPU"

#include <arch/interrupts.h>
#include <arch/utils.h>
#include <arch/x86/arch.h>
#include <arch/x86/memory.h>
#include <arch/x86/smbios.h>
#include <arch/x86/apic.h>
#include <arch/x86/cpu.h>
#include <arch/x86/pic.h>
#include <arch/x86/vbe.h>
#include <debug.h>
#include <machine.h>
#include <string.h>

#if defined(__i386__)
#include <arch/x86/x32/gdt.h>
#include <arch/x86/x32/idt.h>
#else
#include <arch/x86/x64/gdt.h>
#include <arch/x86/x64/idt.h>
#endif

#include "../../../components/cpu_private.h"

#if defined(_MSC_VER) && !defined(__clang__)
#include <intrin.h>
#define __get_cpuid(Function, Registers) __cpuid(Registers, Function);
#else
#include <cpuid.h>
#define __get_cpuid(Function, Registers) __cpuid(Function, (Registers)[0], (Registers)[1], (Registers)[2], (Registers)[3]);
#endif
#define isspace(c) (((c) >= 0x09 && (c) <= 0x0D) || ((c) == 0x20))

extern void __wbinvd(void);
extern void __hlt(void);
extern void memory_invalidate_addr(uintptr_t);
extern void memory_reload_cr3(void);
extern void CpuEnableXSave(void);
extern void CpuEnableAvx(void);
extern void CpuEnableSse(void);
extern void CpuEnableGpe(void);
extern void CpuEnableFpu(void);
extern void _rdmsr(size_t reg, uint64_t* value);
extern void _wrmsr(size_t reg, uint64_t* value);

/* TrimWhitespaces
 * Trims leading and trailing whitespaces in-place on the given string. This is neccessary
 * because of how x86 cpu's store their brand string (with middle alignment?)..*/
static char*
TrimWhitespaces(char *str)
{
    char *end;

    // Trim leading space
    while (isspace((unsigned char)*str)) str++;
    if(*str == 0) {
        return str; // All spaces?
    }

    // Trim trailing space
    end = str + strlen(str) - 1;
    while (end > str && isspace((unsigned char)*end)) end--;

    // Write new null terminator
    *(end + 1) = 0;
    return str;
}

static void
__ExtractCoreTopology(
    _In_  void* Brand,
    _Out_ int*  CoreBits,
    _Out_ int*  LogicalBits)
{
	uint32_t cpuRegisters[4] = {0 };
	
    if (!strncmp(Brand, CPUID_VENDOR_OLDAMD, 12) ||
        !strncmp(Brand, CPUID_VENDOR_AMD, 12)) {
        __get_cpuid(0x80000008, cpuRegisters);
    }
    else if (!strncmp(Brand, CPUID_VENDOR_INTEL, 12)) {
        __get_cpuid(0xB, cpuRegisters);
        *LogicalBits = 0;
    }
    // https://wiki.osdev.org/Detecting_CPU_Topology_(80x86)
}

void
ArchPlatformInitialize(
        _In_ SystemCpu_t*     cpu,
        _In_ SystemCpuCore_t* core)
{
	uint32_t cpuRegisters[4]    = { 0 };
    char     temporaryBrand[64] = { 0 };
    char*    brandPointer = &temporaryBrand[0];
    int      coreBits;
    int      logicalBits;

    __get_cpuid(0, cpuRegisters);
    
    // Set default id of the primary core
    core->Id = 0;
    
    //logical_CPU_number_within_core = APIC_ID & ( (1 << logical_CPU_bits) -1)
    //core_number_within_chip = (APIC_ID >> logical_CPU_bits) & ( (1 << core_bits) -1)
    //chip_ID = APIC_ID & ~( (1 << (logical_CPU_bits+core_bits) ) -1)

    // Store cpu-id level and store the cpu vendor
    cpu->PlatformData.MaxLevel = cpuRegisters[0];
    memcpy(&cpu->Vendor[0], &cpuRegisters[1], 4);
    memcpy(&cpu->Vendor[4], &cpuRegisters[3], 4);
    memcpy(&cpu->Vendor[8], &cpuRegisters[2], 4);

    // Does it support retrieving features?
    if (cpu->PlatformData.MaxLevel >= 1) {
        __get_cpuid(1, cpuRegisters);
        cpu->PlatformData.EcxFeatures = cpuRegisters[2];
        cpu->PlatformData.EdxFeatures = cpuRegisters[3];
        if (cpuRegisters[3] & CPUID_FEAT_EDX_HTT) {
            cpu->NumberOfCores = (int)((cpuRegisters[1] >> 16) & 0xFF);
            core->Id           = (cpuRegisters[1] >> 24) & 0xFF;
        }
        
        // This can be reported as 0, which means we assume a single cpu
        if (cpu->NumberOfCores == 0) {
            cpu->NumberOfCores = 1;
        }
    }
    
    // Get core bits and logical bits
    if (cpu->NumberOfCores != 1) {
        //ExtractCoreTopology(&Processor->Vendor[0], &CoreBits, &LogicalBits);
    }

    // Get extensions supported
    __get_cpuid(0x80000000, cpuRegisters);

    // Extract the processor brand string if it's supported
    cpu->PlatformData.MaxLevelExtended = cpuRegisters[0];
    if (cpu->PlatformData.MaxLevelExtended >= 0x80000004) {
        __get_cpuid(0x80000002, cpuRegisters); // First 16 bytes
        memcpy(&temporaryBrand[0], &cpuRegisters[0], 16);
        __get_cpuid(0x80000003, cpuRegisters); // Middle 16 bytes
        memcpy(&temporaryBrand[16], &cpuRegisters[0], 16);
        __get_cpuid(0x80000004, cpuRegisters); // Last 16 bytes
        memcpy(&temporaryBrand[32], &cpuRegisters[0], 16);
        brandPointer = TrimWhitespaces(brandPointer);
        memcpy(&cpu->Brand[0], brandPointer, strlen(brandPointer));
    }

    // Initialize kernel GDT/IDT and perform some other things while we are doing pre-memory setup.
    if (cpu == &GetMachine()->Processor) {
        GdtInitialize();
        IdtInitialize();
        PicInitialize();
        OutputInitialize();
        SmBiosInitialize();
    }

    // Enable cpu features, this will also install the GS bases. We have to do this after loading the
    // GS/FS segment descriptors, otherwise they will clear the addresses in 64 bit.
    CpuInitializeFeatures();
}

void
SetMachineUmaMode(void)
{
    // Determine the state of the pc. 
    // Are MP tables present?
    // Do we need to use the PIC instead of the APIC?
    
    WARNING("SetMachineUmaMode::end for now");
    for(;;);
}

oserr_t
ArchProcessorSendInterrupt(
        _In_ uuid_t coreId,
        _In_ uuid_t interruptId)
{
    oserr_t osStatus = ApicSendInterrupt(InterruptTarget_SPECIFIC, coreId, interruptId & 0xFF);
    if (osStatus != OsOK) {
        FATAL(FATAL_SCOPE_KERNEL, "Failed to deliver IPI signal");
    }
    return osStatus;
}

void
CpuInitializeFeatures(void)
{
    // Can we use global pages? We will use this for kernel mappings
    // to speed up refill performance
    if (CpuHasFeatures(0, CPUID_FEAT_EDX_PGE) == OsOK) {
        CpuEnableGpe();
    }

	// Can we enable FPU?
	if (CpuHasFeatures(0, CPUID_FEAT_EDX_FPU) == OsOK) {
		CpuEnableFpu();
	}

	// Can we enable SSE?
	if (CpuHasFeatures(0, CPUID_FEAT_EDX_SSE) == OsOK) {
		CpuEnableSse();
	}
    
    // Can we enable xsave? (and maybe avx?)
    if (CpuHasFeatures(CPUID_FEAT_ECX_XSAVE | CPUID_FEAT_ECX_OSXSAVE, 0) == OsOK) {
        CpuEnableXSave();
        if (CpuHasFeatures(CPUID_FEAT_ECX_AVX, 0) == OsOK) {
            CpuEnableAvx();
        }
    }

#ifdef __amd64__
    // In 64 bit mode we want to set the GS-base for the OS. The reason
    // we fill the kernel GS base with the user-one is that we start in kernel mode
    // and don't want the user-one swapped in untill later
    uint64_t userGsBase = MEMORY_SEGMENT_GS_USER_BASE;
    uint64_t kernGsBase = MEMORY_LOCATION_TLS_START;
    CpuWriteModelRegister(CPU_MSR_KERNEL_GS_BASE, &userGsBase);
    CpuWriteModelRegister(CPU_MSR_GS_BASE, &kernGsBase);
#endif
}

oserr_t
CpuHasFeatures(
        _In_ unsigned int ecx,
        _In_ unsigned int edx)
{
	// Check ECX features @todo multiple cpus
	if (ecx != 0) {
		if ((GetMachine()->Processor.PlatformData.EcxFeatures & ecx) != ecx) {
			return OsError;
		}
	}

	// Check EDX features @todo multiple cpus
	if (edx != 0) {
		if ((GetMachine()->Processor.PlatformData.EdxFeatures & edx) != edx) {
			return OsError;
		}
	}
	return OsOK;
}

uuid_t
ArchGetProcessorCoreId(void)
{
    if (ApicIsInitialized() == OsOK) {
        return (ApicReadLocal(APIC_PROCESSOR_ID) >> 24) & 0xFF;
    }

    // If the local apic is not initialized this is single-core old system
    // OR we are still in startup phase, and thus we just return the boot-core
    if (!GetMachine()->Processor.Cores) {
        return 0;
    }
    return GetMachine()->Processor.Cores->Id;
}

void
ArchProcessorIdle(void)
{
	__hlt();
}

void
ArchProcessorHalt(void)
{
	InterruptDisable();
	__hlt();
}

void
CpuFlushInstructionCache(
    _In_Opt_ void*  Start, 
    _In_Opt_ size_t Length) 
{
    // Unused
    _CRT_UNUSED(Start);
    _CRT_UNUSED(Length);

    // Invoke assembly routine
    __wbinvd();
}

void
CpuInvalidateMemoryCache(
    _In_Opt_ void*  Start, 
    _In_Opt_ size_t Length)
{
    if (Start == NULL) {
        // TODO: disable PGE bit
        memory_reload_cr3();
    }
    else {
        uintptr_t Offset       = ((uintptr_t)Start) & ATTRIBUTE_MASK;
        size_t    AdjustLength = Offset + Length;
        uintptr_t StartAddress = ((uintptr_t)Start) & PAGE_MASK;
        uintptr_t EndAddress   = ((StartAddress + AdjustLength) & PAGE_MASK) + PAGE_SIZE;
        for (; StartAddress < EndAddress; StartAddress += PAGE_SIZE) {
            memory_invalidate_addr(StartAddress);
        }
    }
}

void CpuReadModelRegister(uint32_t registerIndex, uint64_t* pointerToValue)
{
    if (CpuHasFeatures(0, CPUID_FEAT_EDX_MSR) == OsOK) {
        _rdmsr(registerIndex, pointerToValue);
    }
    else {
        ERROR("[read_msr] MSR is not supported on this cpu: %u", registerIndex);
    }
}

void CpuWriteModelRegister(uint32_t registerIndex, uint64_t* pointerToValue)
{
    if (CpuHasFeatures(0, CPUID_FEAT_EDX_MSR) == OsOK) {
        _wrmsr(registerIndex, pointerToValue);
    }
    else {
        ERROR("[write_msr] MSR is not supported on this cpu: %u", registerIndex);
    }
}

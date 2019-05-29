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
 * x86 Cpu Information Header
 * - Contains some definitions and structures for helping around
 *   in the sub-layer system
 */
#define __MODULE "CCPU"

#include <arch/interrupts.h>
#include <arch/utils.h>
#include <interrupts.h>
#include <machine.h>
#include <memory.h>
#include <smbios.h>
#include <debug.h>
#include <apic.h>
#include <cpu.h>
#include <gdt.h>
#include <idt.h>
#include <pic.h>
#include <vbe.h>

#if defined(_MSC_VER) && !defined(__clang__)
#include <intrin.h>
#define __get_cpuid(Function, Registers) __cpuid(Registers, Function);
#else
#include <cpuid.h>
#define __get_cpuid(Function, Registers) __cpuid(Function, Registers[0], Registers[1], Registers[2], Registers[3]);
#endif
#define isspace(c) ((c >= 0x09 && c <= 0x0D) || (c == 0x20))

extern void __wbinvd(void);
extern void __hlt(void);
extern void memory_invalidate_addr(uintptr_t);
extern void memory_reload_cr3(void);
extern void CpuEnableXSave(void);
extern void CpuEnableAvx(void);
extern void CpuEnableSse(void);
extern void CpuEnableGpe(void);
extern void CpuEnableFpu(void);

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

void
ArchProcessorInitialize(
    _In_ SystemCpu_t* Processor)
{
	// Variables
	uint32_t CpuRegisters[4]    = { 0 };
    char TemporaryBrand[64]     = { 0 };
    char *BrandPointer          = &TemporaryBrand[0];
    __get_cpuid(0, CpuRegisters);
    
    // Set default number of cores params
    Processor->NumberOfCores          = 1;

    // Initialize the default params of primary core
    Processor->PrimaryCore.Id         = 0;
    Processor->PrimaryCore.State      = CpuStateRunning;
    Processor->PrimaryCore.External   = 0;

    // Store cpu-id level and store the cpu vendor
    Processor->Data[CPU_DATA_MAXLEVEL] = CpuRegisters[0];
    memcpy(&Processor->Vendor[0], &CpuRegisters[1], 4);
    memcpy(&Processor->Vendor[4], &CpuRegisters[3], 4);
    memcpy(&Processor->Vendor[8], &CpuRegisters[2], 4);

    // Does it support retrieving features?
    if (Processor->Data[CPU_DATA_MAXLEVEL] >= 1) {
        __get_cpuid(1, CpuRegisters);
        Processor->Data[CPU_DATA_FEATURES_ECX]    = CpuRegisters[2];
        Processor->Data[CPU_DATA_FEATURES_EDX]    = CpuRegisters[3];
        Processor->NumberOfCores                  = (CpuRegisters[1] >> 16) & 0xFF;
        Processor->PrimaryCore.Id                 = (CpuRegisters[1] >> 24) & 0xFF;

        // This can be reported as 0, which means we assume a single cpu
        if (Processor->NumberOfCores == 0) {
            Processor->NumberOfCores = 1;
        }
    }

    // Get extensions supported
    __get_cpuid(0x80000000, CpuRegisters);

    // Extract the processor brand string if it's supported
    Processor->Data[CPU_DATA_MAXEXTENDEDLEVEL] = CpuRegisters[0];
    if (Processor->Data[CPU_DATA_MAXEXTENDEDLEVEL] >= 0x80000004) {
        __get_cpuid(0x80000002, CpuRegisters); // First 16 bytes
        memcpy(&TemporaryBrand[0], &CpuRegisters[0], 16);
        __get_cpuid(0x80000003, CpuRegisters); // Middle 16 bytes
        memcpy(&TemporaryBrand[16], &CpuRegisters[0], 16);
        __get_cpuid(0x80000004, CpuRegisters); // Last 16 bytes
        memcpy(&TemporaryBrand[32], &CpuRegisters[0], 16);
        BrandPointer = TrimWhitespaces(BrandPointer);
        memcpy(&Processor->Brand[0], BrandPointer, strlen(BrandPointer));
    }

    // Enable cpu features
    CpuInitializeFeatures();

    // Initialize cpu systems, only do this for primary processor
    // @todo
    GdtInitialize();
    IdtInitialize();
    PicInitialize();
    VbeInitialize();
    SmBiosInitialize(NULL);
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

void
ArchProcessorSendInterrupt(
    _In_ UUId_t CoreId,
    _In_ UUId_t InterruptId)
{
    OsStatus_t Status = ApicSendInterrupt(InterruptSpecific, CoreId, InterruptId & 0xFF);
    if (Status != OsSuccess) {
        FATAL(FATAL_SCOPE_KERNEL, "Failed to deliver IPI signal");
    }
}

void
CpuInitializeFeatures(void)
{
    // Can we use global pages? We will use this for kernel mappings
    // to speed up refill performance
    if (CpuHasFeatures(0, CPUID_FEAT_EDX_PGE) == OsSuccess) {
        CpuEnableGpe();
    }

	// Can we enable FPU?
	if (CpuHasFeatures(0, CPUID_FEAT_EDX_FPU) == OsSuccess) {
		CpuEnableFpu();
	}

	// Can we enable SSE?
	if (CpuHasFeatures(0, CPUID_FEAT_EDX_SSE) == OsSuccess) {
		CpuEnableSse();
	}
    
    // Can we enable xsave? (and maybe avx?)
    if (CpuHasFeatures(CPUID_FEAT_ECX_XSAVE | CPUID_FEAT_ECX_OSXSAVE, 0) == OsSuccess) {
        CpuEnableXSave();
        if (CpuHasFeatures(CPUID_FEAT_ECX_AVX, 0) == OsSuccess) {
            CpuEnableAvx();
        }
    }
}

OsStatus_t
CpuHasFeatures(Flags_t Ecx, Flags_t Edx)
{
	// Check ECX features @todo multiple cpus
	if (Ecx != 0) {
		if ((GetMachine()->Processor.Data[CPU_DATA_FEATURES_ECX] & Ecx) != Ecx) {
			return OsError;
		}
	}

	// Check EDX features @todo multiple cpus
	if (Edx != 0) {
		if ((GetMachine()->Processor.Data[CPU_DATA_FEATURES_EDX] & Edx) != Edx) {
			return OsError;
		}
	}
	return OsSuccess;
}

UUId_t
ArchGetProcessorCoreId(void)
{
    if (ApicIsInitialized() == OsSuccess) {
        return (ApicReadLocal(APIC_PROCESSOR_ID) >> 24) & 0xFF;
    }

    // If the local apic is not initialized this is single-core old system
    // OR we are still in startup phase and thus we just return the boot-core
    return GetMachine()->Processor.PrimaryCore.Id;
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
        memory_reload_cr3();
    }
    else {
        uintptr_t StartAddress = ((uintptr_t)Start) & PAGE_MASK;
        uintptr_t EndAddress   = StartAddress + Length + PAGE_MASK;
        for (uintptr_t i = StartAddress; i < EndAddress; i += PAGE_SIZE) {
            memory_invalidate_addr(i);
        }
    }
}

extern void _rdtsc(uint64_t *Value);

void
ArchStallProcessorCore(
    size_t MilliSeconds)
{
	// Variables
	volatile uint64_t TimeOut = 0;
	uint64_t Counter = 0;

	if (!(GetMachine()->Processor.Data[CPU_DATA_FEATURES_EDX] & CPUID_FEAT_EDX_TSC)) {
		FATAL(FATAL_SCOPE_KERNEL, "DelayMs() was called, but no TSC support in CPU.");
		ArchProcessorIdle();
	}

	// Use the read timestamp counter
	_rdtsc(&Counter);
	TimeOut = Counter + (uint64_t)(MilliSeconds * 100000);
	while (Counter < TimeOut) { _rdtsc(&Counter); }
}

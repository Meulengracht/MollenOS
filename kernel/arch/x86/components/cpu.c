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
 * MollenOS X86 Cpu Information Header
 * - Contains some definitions and structures for helping around
 *   in the sub-layer system
 */

/* Includes
 * - System */
#include <system/interrupts.h>
#include <system/utils.h>
#include <interrupts.h>
#include <log.h>
#include <cpu.h>

#if defined(_MSC_VER) && !defined(__clang__)
#include <intrin.h>
#define __get_cpuid(Function, Registers) __cpuid(Registers, Function);
#else
#include <cpuid.h>
#define __get_cpuid(Function, Registers) __cpuid(Function, Registers[0], Registers[1], Registers[2], Registers[3]);
#endif

 /* Globals 
  * Keep a copy of cpu information in the system for the boot cpu */
static CpuInformation_t __CpuInformation = { 0 };
static int __CpuInitialized = 0;

/* Extern functions
 * We need access to these in order to implement
 * the interface */
__EXTERN volatile size_t GlbTimerTicks[64];
__EXTERN UUId_t ApicGetCpu(void);

/* Extern assembly functions
 * These utilities are located in boot.asm */
__EXTERN void __wbinvd(void);
__EXTERN void __hlt(void);
__EXTERN void CpuEnableXSave(void);
__EXTERN void CpuEnableAvx(void);
__EXTERN void CpuEnableSse(void);
__EXTERN void CpuEnableFpu(void);

/* CpuInitialize
 * Initializes the CPU and gathers available
 * information about it */
void 
CpuInitialize(void)
{
	// Variables
	int CpuRegisters[4] = { 0 };

	// Has init already run?
	if (__CpuInitialized != 1) {
	    __get_cpuid(0, CpuRegisters);

		// Store cpu-id level
		__CpuInformation.CpuIdLevel = CpuRegisters[0];

		// Does it support retrieving features?
		if (__CpuInformation.CpuIdLevel >= 1) {
	        __get_cpuid(1, CpuRegisters);
			__CpuInformation.EcxFeatures = CpuRegisters[2];
			__CpuInformation.EdxFeatures = CpuRegisters[3];
		}

		// Get extensions supported
	    __get_cpuid(0x80000000, CpuRegisters);

		// Store them
		__CpuInformation.CpuIdExtensions = CpuRegisters[0];
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

/* CpuHasFeatures
 * Determines if the cpu has the requested features */
OsStatus_t
CpuHasFeatures(Flags_t Ecx, Flags_t Edx)
{
	// Check ECX features
	if (Ecx != 0) {
		if ((__CpuInformation.EcxFeatures & Ecx) != Ecx) {
			return OsError;
		}
	}

	// Check EDX features
	if (Edx != 0) {
		if ((__CpuInformation.EdxFeatures & Edx) != Edx) {
			return OsError;
		}
	}

	// All requested features present
	return OsSuccess;
}

/* CpuGetCurrentId 
 * Retrieves the current cpu id for caller */
UUId_t
CpuGetCurrentId(void)
{
	// Get apic id
	return ApicGetCpu();
}

/* CpuIdle
 * Enters idle mode for the current cpu */
void
CpuIdle(void)
{
	// Don't disable interrupts
	// simply just wait for the next
	__hlt();
}

/* CpuHalt
 * Halts the current cpu - rendering cpu useless */
void
CpuHalt(void)
{
	// Disable interrupts and idle
	InterruptDisable();
	__hlt();
}

/* CpuGetTicks
 * Get the ticks for the current cpu. */
size_t
CpuGetTicks(void) {
    return GlbTimerTicks[CpuGetCurrentId()];
}

/* CpuFlushInstructionCache
 * Flushes the instruction cache for the processor. */
void
CpuFlushInstructionCache(
    _In_Opt_ void*  Start, 
    _In_Opt_ size_t Length) {
    // Unused
    _CRT_UNUSED(Start);
    _CRT_UNUSED(Length);

    // Invoke assembly routine
    __wbinvd();
}

/* Backup Timer, Should always be provided */
extern void _rdtsc(uint64_t *Value);

/* CpuStall
 * Stalls the cpu for the given milliseconds, blocking call. */
void
CpuStall(
    size_t MilliSeconds)
{
	// Variables
	volatile uint64_t TimeOut = 0;
	uint64_t Counter = 0;

	if (!(__CpuInformation.EdxFeatures & CPUID_FEAT_EDX_TSC)) {
		LogFatal("TIMR", "DelayMs() was called, but no TSC support in CPU.");
		CpuIdle();
	}

	// Use the read timestamp counter
	_rdtsc(&Counter);
	TimeOut = Counter + (uint64_t)(MilliSeconds * 100000);
	while (Counter < TimeOut) { _rdtsc(&Counter); }
}

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
#include <system/utils.h>
#include <interrupts.h>
#include <log.h>
#include <cpu.h>

 /* Globals 
  * Keep a copy of cpu information in the system */
static CpuInformation_t __CpuInformation = { 0 };
static int __CpuInitialized = 0;

/* Extern functions
 * We need access to these in order to implement
 * the interface */
__EXTERN UUId_t ApicGetCpu(void);

/* Extern assembly functions
 * These utilities are located in boot.asm */
__EXTERN void __hlt(void);
__EXTERN void CpuEnableSse(void);
__EXTERN void CpuEnableFpu(void);
__EXTERN void CpuId(uint32_t CpuId, uint32_t *Eax, uint32_t *Ebx, uint32_t *Ecx, uint32_t *Edx);

/* CpuInitialize
 * Initializes the CPU and gathers available
 * information about it */
void 
CpuInitialize(void)
{
	// Variables
	uint32_t _eax, _ebx, _ecx, _edx;

	// Has init already run?
	if (__CpuInitialized != 1) {

		// Get base cpu-id information
		CpuId(0, &_eax, &_ebx, &_ecx, &_edx);

		// Store cpu-id level
		__CpuInformation.CpuIdLevel = _eax;

		// Does it support retrieving features?
		if (__CpuInformation.CpuIdLevel >= 1) {
			CpuId(1, &_eax, &_ebx, &_ecx, &_edx);
			__CpuInformation.EcxFeatures = _ecx;
			__CpuInformation.EdxFeatures = _edx;
		}

		// Get extensions supported
		CpuId(0x80000000, &_eax, &_ebx, &_ecx, &_edx);

		// Store them
		__CpuInformation.CpuIdExtensions = _eax;
	}

	// Can we enable FPU?
	if (__CpuInformation.EdxFeatures & CPUID_FEAT_EDX_FPU) {
		CpuEnableFpu();
	}

	// Can we enable SSE?
	if (__CpuInformation.EdxFeatures & CPUID_FEAT_EDX_SSE) {
		CpuEnableSse();
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
	return OsNoError;
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

/* Backup Timer, Should always be provided */
extern void rdtsc(uint64_t *Value);

void DelayMs(uint32_t MilliSeconds)
{
	/* Keep value in this */
	uint64_t Counter = 0;
	volatile uint64_t TimeOut = 0;

	/* Sanity */
	if (!(__CpuInformation.EdxFeatures & CPUID_FEAT_EDX_TSC))
	{
		LogFatal("TIMR", "DelayMs() was called, but no TSC support in CPU.");
		CpuIdle();
	}

	/* Use rdtsc for this */
	rdtsc(&Counter);
	TimeOut = Counter + (uint64_t)(MilliSeconds * 100000);

	while (Counter < TimeOut) { rdtsc(&Counter); }
}

/* MollenOS
*
* Copyright 2011 - 2016, Philip Meulengracht
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
* MollenOS - Optimized memory copy
*/

#include <math.h>
#define CPUID_FEAT_EDX_SSE		1 << 25
#if defined(_MSC_VER) && !defined(__clang__)
#include <intrin.h>
#else
#include <cpuid.h>
#endif

void _fpreset(void)
{
    // Variables
    const unsigned short x86_cw = 0x27f;
	int CpuRegisters[4] = { 0 };
	int CpuFeatEdx = 0;

	// Now extract the cpu information 
	// so we can select a memcpy
#if defined(_MSC_VER) && !defined(__clang__)
	__cpuid(CpuRegisters, 1);
#else
    __cpuid(1, CpuRegisters[0], CpuRegisters[1], CpuRegisters[2], CpuRegisters[3]);
#endif
    // Features are in ecx/edx
    CpuFeatEdx = CpuRegisters[3];

#ifdef _MSC_VER
    __asm { fninit }
    __asm { fldcw [x86_cw] }
#else
    __asm__ __volatile__( "fninit; fldcw %0" : : "m" (x86_cw) );
#endif
    if (CpuFeatEdx & CPUID_FEAT_EDX_SSE) {
        const unsigned long sse2_cw = 0x1f80;
#ifdef _MSC_VER
        __asm { ldmxcsr [sse2_cw] }
#else
        __asm__ __volatile__( "ldmxcsr %0" : : "m" (sse2_cw) );
#endif
    }
}
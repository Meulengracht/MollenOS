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
*/

#include <stdint.h>
#include <math.h>

#define CPUID_FEAT_EDX_SSE		1 << 25

#if defined(_MSC_VER) && !defined(__clang__)
#include <intrin.h>
#define __get_cpuid(Function, Registers) __cpuid(Registers, Function);
#else
#include <cpuid.h>
#define __get_cpuid(Function, Registers) __cpuid(Function, Registers[0], Registers[1], Registers[2], Registers[3]);
#endif

void _fpreset(void)
{
    const unsigned long  sse2_cw          = 0x1f80;
    const unsigned short x86_cw           = 0x27f;
	uint32_t             cpu_registers[4] = { 0 };

    __get_cpuid(0, cpu_registers);
#if defined(_MSC_VER) && !defined(__clang__)
    __asm { fninit }
    __asm { fldcw [x86_cw] }
#else
    __asm__ __volatile__( "fninit; fldcw %0" : : "m" (x86_cw) );
#endif
    if (cpu_registers[3] & CPUID_FEAT_EDX_SSE) {
#if defined(_MSC_VER) && !defined(__clang__)
        __asm { ldmxcsr [sse2_cw] }
#else
        __asm__ __volatile__( "ldmxcsr %0" : : "m" (sse2_cw) );
#endif
    }
}

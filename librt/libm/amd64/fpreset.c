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

void _fpreset(void)
{
    // Variables
    const unsigned short x86_cw = 0x27f;
    const unsigned long sse2_cw = 0x1f80;
	
#ifdef _MSC_VER
    __asm { fninit }
    __asm { fldcw [x86_cw] }
    __asm { ldmxcsr [sse2_cw] }
#else
    __asm__ __volatile__( "fninit; fldcw %0" : : "m" (x86_cw) );
    __asm__ __volatile__( "ldmxcsr %0" : : "m" (sse2_cw) );
#endif
}
/* MollenOS
*
* Copyright 2011 - 2014, Philip Meulengracht
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
* MollenOS x86-32 CPU Setup
*/

/* Includes */
#include <arch.h>
#include <cpu.h>
#include <stdio.h>

/* Globals */
cpu_info_t boot_cpu_info;

void cpu_boot_init(void)
{
	/* Get CPUID Information */
	uint32_t _eax, _ebx, _ecx, _edx;
	char *cpu_brand = boot_cpu_info.cpu_brand;
	char *cpu_vendor = boot_cpu_info.cpu_manufacter;

	/* Get initial CPUID */
	cpuid(0, &_eax, &_ebx, &_ecx, &_edx);

	/* Set info */
	boot_cpu_info.cpuid_level = _eax;
	*(addr_t*)(cpu_vendor + 0) = _ebx;
	*(addr_t*)(cpu_vendor + 4) = _edx;
	*(addr_t*)(cpu_vendor + 8) = _ecx;

	/* Get next batch of CPUID if supported */
	if (boot_cpu_info.cpuid_level >= 1)
	{
		/* Yay, one more batch */
		cpuid(1, &_eax, &_ebx, &_ecx, &_edx);

		/* Set info */
		boot_cpu_info.ecx_features = _ecx;
		boot_cpu_info.edx_features = _edx;

		boot_cpu_info.cpu_stepping = (char)(_eax & 0x0F);
		boot_cpu_info.cpu_model = (char)((_eax >> 4) & 0x0F);
		boot_cpu_info.cpu_family = (char)((_eax >> 8) & 0x0F);
		boot_cpu_info.cpu_type = (char)((_eax >> 12) & 0x03);

		boot_cpu_info.cpu_cache_size = (char)((_ebx >> 8) & 0xFF) * 8; /* cache_line_size * 8 = size in bytes */
		boot_cpu_info.cpu_num_logical_processors = (char)((_ebx >> 16) & 0xFF);    /* # logical cpu's per physical cpu */
		boot_cpu_info.cpu_lapic_id = (char)((_ebx >> 24) & 0xFF);    /* Local APIC ID */
	}

	/* Check if cpu supports brand call */
	cpuid(0x80000000, &_eax, &_ebx, &_ecx, &_edx);

	/* Set info */
	boot_cpu_info.cpuid_extensions = _eax;

	/* Check for last cpuid batch */
	if (boot_cpu_info.cpuid_extensions >= 0x80000004)
	{
		/* Yay! Get brand */
		cpuid(0x80000002, &_eax, &_ebx, &_ecx, &_edx);

		//First load of registers, 0x80000002
		*(addr_t*)(cpu_brand + 0) = _eax;
		*(addr_t*)(cpu_brand + 4) = _ebx;
		*(addr_t*)(cpu_brand + 8) = _ecx;
		*(addr_t*)(cpu_brand + 12) = _edx;

		cpuid(0x80000003, &_eax, &_ebx, &_ecx, &_edx);

		//Second load of registers, 0x80000003
		*(addr_t*)(cpu_brand + 16) = _eax;
		*(addr_t*)(cpu_brand + 20) = _ebx;
		*(addr_t*)(cpu_brand + 24) = _ecx;
		*(addr_t*)(cpu_brand + 28) = _edx;

		cpuid(0x80000004, &_eax, &_ebx, &_ecx, &_edx);

		//Last load of registers, 0x80000004
		*(addr_t*)(cpu_brand + 32) = _eax;
		*(addr_t*)(cpu_brand + 36) = _ebx;
		*(addr_t*)(cpu_brand + 40) = _ecx;
		*(addr_t*)(cpu_brand + 44) = _edx;
	}

	/* Enable FPU */
	if (boot_cpu_info.edx_features & CPUID_FEAT_EDX_FPU)
		enable_fpu();

	/* Enable SSE */
	if (boot_cpu_info.edx_features & CPUID_FEAT_EDX_SSE)
		enable_sse();
}
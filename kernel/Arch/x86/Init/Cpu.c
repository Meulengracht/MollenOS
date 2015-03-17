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
#include <Arch.h>
#include <Cpu.h>
#include <stdio.h>

/* Globals */
CpuObject_t boot_cpu_info;

void CpuBspInit(void)
{
	/* Get CPUID Information */
	uint32_t _eax, _ebx, _ecx, _edx;
	char *cpu_brand = boot_cpu_info.CpuBrand;
	char *cpu_vendor = boot_cpu_info.CpuManufacter;

	/* Get initial CPUID */
	CpuId(0, &_eax, &_ebx, &_ecx, &_edx);

	/* Set info */
	boot_cpu_info.CpuIdLevel = _eax;
	*(Addr_t*)(cpu_vendor + 0) = _ebx;
	*(Addr_t*)(cpu_vendor + 4) = _edx;
	*(Addr_t*)(cpu_vendor + 8) = _ecx;

	/* Get next batch of CPUID if supported */
	if (boot_cpu_info.CpuIdLevel >= 1)
	{
		/* Yay, one more batch */
		CpuId(1, &_eax, &_ebx, &_ecx, &_edx);

		/* Set info */
		boot_cpu_info.EcxFeatures = _ecx;
		boot_cpu_info.EdxFeatures = _edx;

		boot_cpu_info.CpuStepping = (char)(_eax & 0x0F);
		boot_cpu_info.CpuModel = (char)((_eax >> 4) & 0x0F);
		boot_cpu_info.CpuFamily = (char)((_eax >> 8) & 0x0F);
		boot_cpu_info.CpuType = (char)((_eax >> 12) & 0x03);

		boot_cpu_info.CpuCacheSize = (char)((_ebx >> 8) & 0xFF) * 8; /* cache_line_size * 8 = size in bytes */
		boot_cpu_info.CpuNumLogicalProessors = (char)((_ebx >> 16) & 0xFF);    /* # logical cpu's per physical cpu */
		boot_cpu_info.CpuLApicId = (char)((_ebx >> 24) & 0xFF);    /* Local APIC ID */
	}

	/* Check if cpu supports brand call */
	CpuId(0x80000000, &_eax, &_ebx, &_ecx, &_edx);

	/* Set info */
	boot_cpu_info.CpuIdExtensions = _eax;

	/* Check for last cpuid batch */
	if (boot_cpu_info.CpuIdExtensions >= 0x80000004)
	{
		/* Yay! Get brand */
		CpuId(0x80000002, &_eax, &_ebx, &_ecx, &_edx);

		//First load of registers, 0x80000002
		*(Addr_t*)(cpu_brand + 0) = _eax;
		*(Addr_t*)(cpu_brand + 4) = _ebx;
		*(Addr_t*)(cpu_brand + 8) = _ecx;
		*(Addr_t*)(cpu_brand + 12) = _edx;

		CpuId(0x80000003, &_eax, &_ebx, &_ecx, &_edx);

		//Second load of registers, 0x80000003
		*(Addr_t*)(cpu_brand + 16) = _eax;
		*(Addr_t*)(cpu_brand + 20) = _ebx;
		*(Addr_t*)(cpu_brand + 24) = _ecx;
		*(Addr_t*)(cpu_brand + 28) = _edx;

		CpuId(0x80000004, &_eax, &_ebx, &_ecx, &_edx);

		//Last load of registers, 0x80000004
		*(Addr_t*)(cpu_brand + 32) = _eax;
		*(Addr_t*)(cpu_brand + 36) = _ebx;
		*(Addr_t*)(cpu_brand + 40) = _ecx;
		*(Addr_t*)(cpu_brand + 44) = _edx;
	}

	/* Enable FPU */
	if (boot_cpu_info.EdxFeatures & CPUID_FEAT_EDX_FPU)
		CpuEnableFpu();

	/* Enable SSE */
	if (boot_cpu_info.EdxFeatures & CPUID_FEAT_EDX_SSE)
		CpuEnableSse();
}
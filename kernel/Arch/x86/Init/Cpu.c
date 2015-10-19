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

/* MollenOS */
#include <Devices/Cpu.h>

/* Includes */
#include <Arch.h>
#include <stdio.h>

/* Globals */
x86CpuObject_t GlbBootCpuInfo;

/* Externs */
extern void __hlt(void);

/* These three are located in boot.asm */
extern void CpuEnableSse(void);
extern void CpuEnableFpu(void);
extern void CpuId(uint32_t CpuId, uint32_t *Eax, uint32_t *Ebx, uint32_t *Ecx, uint32_t *Edx);

void _CpuSetup(void *CpuData, void *BootInfo)
{
	/* Cast */
	MCoreCpuDevice_t *mCpu = (MCoreCpuDevice_t*)CpuData;
	mCpu->Data = (void*)&GlbBootCpuInfo;
	mCpu->Id = 0;

	/* Not used */
	_CRT_UNUSED(BootInfo);

	/* Get CPUID Information */
	uint32_t _eax, _ebx, _ecx, _edx;
	char *cpu_brand = mCpu->Brand;
	char *cpu_vendor = mCpu->Manufacter;

	/* Get initial CPUID */
	CpuId(0, &_eax, &_ebx, &_ecx, &_edx);

	/* Set info */
	GlbBootCpuInfo.CpuIdLevel = _eax;
	*(Addr_t*)(cpu_vendor + 0) = _ebx;
	*(Addr_t*)(cpu_vendor + 4) = _edx;
	*(Addr_t*)(cpu_vendor + 8) = _ecx;

	/* Get next batch of CPUID if supported */
	if (GlbBootCpuInfo.CpuIdLevel >= 1)
	{
		/* Yay, one more batch */
		CpuId(1, &_eax, &_ebx, &_ecx, &_edx);

		/* Set info */
		GlbBootCpuInfo.EcxFeatures = _ecx;
		GlbBootCpuInfo.EdxFeatures = _edx;

		/* Update Device */
		mCpu->Stepping = (char)(_eax & 0x0F);
		mCpu->Model = (char)((_eax >> 4) & 0x0F);
		mCpu->Family = (char)((_eax >> 8) & 0x0F);
		mCpu->Type = (char)((_eax >> 12) & 0x03);
		
		/* cache_line_size * 8 = size in bytes */
		mCpu->CacheSize = (char)((_ebx >> 8) & 0xFF) * 8; 

		/* # logical cpu's per physical cpu */
		mCpu->NumLogicalProessors = (char)((_ebx >> 16) & 0xFF);    
		
		/* Local APIC ID */
		GlbBootCpuInfo.CpuLApicId = (char)((_ebx >> 24) & 0xFF);    
	}

	/* Check if cpu supports brand call */
	CpuId(0x80000000, &_eax, &_ebx, &_ecx, &_edx);

	/* Set info */
	GlbBootCpuInfo.CpuIdExtensions = _eax;

	/* Check for last cpuid batch */
	if (GlbBootCpuInfo.CpuIdExtensions >= 0x80000004)
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
	if (GlbBootCpuInfo.EdxFeatures & CPUID_FEAT_EDX_FPU)
		CpuEnableFpu();

	/* Enable SSE */
	if (GlbBootCpuInfo.EdxFeatures & CPUID_FEAT_EDX_SSE)
		CpuEnableSse();
}

/* Idles using HALT */
void Idle(void)
{
	__hlt();
}
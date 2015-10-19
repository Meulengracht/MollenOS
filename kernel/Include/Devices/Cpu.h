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
* MollenOS Cpu Device
*/

#ifndef _MCORE_DEVICE_CPU_H_
#define _MCORE_DEVICE_CPU_H_

/* CPU Includes */
#include <MollenOS.h>

/* CPU Structures */
typedef struct _MCoreCpuDevice
{
	/* Names */
	char Brand[64];
	char Manufacter[32];

	/* Specs */
	uint16_t Id;
	uint8_t Stepping;
	uint8_t Model;
	uint8_t Family;
	uint8_t Type;
	uint8_t CacheSize;
	uint8_t NumLogicalProessors;
	
	/* Architecture Data */
	void *Data;

} MCoreCpuDevice_t;

/* CPU Prototypes */
_CRT_EXTERN OsResult_t CpuInit(MCoreCpuDevice_t *OutData, void *BootInfo);

#endif // !_x86_CPU_H_

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
* Multiboot Structure
*/

#ifndef __MCORE_MULTIBOOT_H__
#define __MCORE_MULTIBOOT_H__

/* Includes */
#include <stdint.h>

/* Multiboot Information Struture */
#pragma pack(push, 1)
typedef struct MultibootInfo
{
	uint32_t Flags;
	uint32_t MemoryLow;
	uint32_t MemoryHigh;
	uint32_t BootDevice;
	uint32_t CmdLine;
	uint32_t ModuleCount;
	uint32_t ModuleAddr;
	
	union
	{
		struct
		{
			/* (a.out) Kernel Symbol table info */
			uint32_t TabSize;
			uint32_t StrSize;
			uint32_t Addr;
			uint32_t Pad;
		} A;
		struct
		{
			/* (ELF) Kernel section header table */
			uint32_t Num;
			uint32_t Size;
			uint32_t Addr;
			uint32_t Shndx;
		} Elf;
	} Symbols;

	/* Memory Mappings */
	uint32_t MemoryMapLength;
	uint32_t MemoryMapAddress;

	/* Drive Info */
	uint32_t DrivesLength;
	uint32_t DrivesAddr;
	
	/* ROM Configuration Table */
	uint32_t ConfigTable;
	
	/* BootLoader Name */
	uint32_t BootLoaderName;

	/* APM Table */
	uint32_t ApmTable;

	/* Video Info */
	uint32_t VbeControllerInfo;
	uint32_t VbeModeInfo;
	uint32_t VbeMode;
	uint32_t VbeInterfaceSegment;
	uint32_t VbeInterfaceOffset;
	uint32_t VbeInterfaceLength;

} Multiboot_t;
#pragma pack(pop)

/* Flags */
#define MB_INFO_MEMORY			0x1
#define MB_INFO_BOOTDEVICE		0x2
#define MB_INFO_CMDLINE			0x4
#define MB_INFO_MODULES			0x8

/* The next two are mutually exclusive */
#define MB_INFO_AOUT			0x10
#define MB_INFO_ELF			0x20

/* More Symbols */
#define MB_INFO_MEM_MAP			0x40
#define MB_INFO_DRIVE_INFO		0x80
#define MB_INFO_CONFIG_TABLE		0x100
#define MB_INFO_BOOT_LDR_NAME		0x200
#define MB_INFO_APM_TABLE		0x400
#define MB_INFO_VIDEO_INFO		0x800

/* R/EAX must contain this */
#define MULTIBOOT_MAGIC			0x2BADBOO2

#endif

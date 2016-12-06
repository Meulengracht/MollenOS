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
* MollenOS x86-32 Global Descriptor Table
*/

#ifndef _x86_GDT_H_
#define _x86_GDT_H_

/* GDT Includes */
#include <Arch.h>
#include <crtdefs.h>
#include <stdint.h>

/* GDT Definitions */
#define X86_GDT_MAX_DESCRIPTORS	16
#define X86_GDT_MAX_TSS			16

#define X86_KERNEL_CODE_SEGMENT 0x08
#define X86_KERNEL_DATA_SEGMENT 0x10
#define X86_USER_CODE_SEGMENT	0x18
#define X86_USER_DATA_SEGMENT	0x20

#define X86_GDT_KERNEL_CODE		0x9A
#define X86_GDT_KERNEL_DATA		0x92
#define X86_GDT_USER_CODE		0xFA
#define X86_GDT_USER_DATA		0xF2
#define X86_GDT_TSS_ENTRY		0xE9

/* GDT Structures */
#pragma pack(push, 1)
typedef struct _GdtObject
{
	/* Size */
	uint16_t Limit;

	/* Pointer to table */
	uint32_t Base;
} Gdt_t;
#pragma pack(pop)

/* GDT Entry */
#pragma pack(push, 1)
typedef struct _GdtEntry
{
	/* Limit 0:15 */
	uint16_t LimitLow;

	/* Base 0:15 */
	uint16_t BaseLow;

	/* Base 16:23 */
	uint8_t BaseMid;

	/* Access Flags */
	uint8_t Access;

	/* Limit high 0:3, Flags 4:7*/
	uint8_t Flags;

	/* Base 24:31 */
	uint8_t BaseHigh;

} GdtEntry_t;
#pragma pack(pop)

/* TSS Entry */
#pragma pack(push, 1)
typedef struct _TssEntry 
{
	/* Link to previous TSS */
	uint32_t	PreviousTSS;

	/* Ring 0 */
	uint32_t	Esp0;
	uint32_t	Ss0;

	/* Ring 1 */
	uint32_t	Esp1;
	uint32_t	Ss1;

	/* Ring 2 */
	uint32_t	Esp2;
	uint32_t	Ss2;

	/* Paging */
	uint32_t	Cr3;

	/* State */
	uint32_t	Eip, EFlags;

	/* Registers */
	uint32_t	Eax, Ecx, Edx, Ebx;
	uint32_t	Esp, Ebp, Esi, Edi;

	/* Segments */
	uint32_t	Es;
	uint32_t	Cs;
	uint32_t	Ss;
	uint32_t	Ds;
	uint32_t	Fs;
	uint32_t	Gs;

	/* Misc */
	uint32_t	Ldt;
	uint16_t	Trap;
	uint16_t	IoMap;
} TssEntry_t;
#pragma pack(pop)

/* GDT Prototypes */
__CRT_EXTERN void GdtInit(void);
__CRT_EXTERN void GdtInstallDescriptor(uint32_t Base, uint32_t Limit,
									  uint8_t Access, uint8_t Grandularity);


/* TSS Prototypes */
__CRT_EXTERN void TssInstall(int GdtIndex);
__CRT_EXTERN void TssUpdateStack(Cpu_t Cpu, Addr_t Stack);

/* Should be called by AP cores */
__CRT_EXTERN void GdtInstall(void);
__CRT_EXTERN void GdtInstallTss(Cpu_t Cpu);

#endif // !_x86_GDT_H_
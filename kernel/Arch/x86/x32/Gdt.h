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
typedef struct gdt_entry
{
	/* Limit 0:15 */
	uint16_t limit_lo;

	/* Base 0:15 */
	uint16_t base_lo;

	/* Base 16:23 */
	uint8_t base_mid;

	/* Access Flags */
	uint8_t access;

	/* Limit high 0:3, Flags 4:7*/
	uint8_t flags;

	/* Base 24:31 */
	uint8_t base_high;

} gdt_entry_t;
#pragma pack(pop)

/* TSS Entry */
#pragma pack(push, 1)
typedef struct tss_entry 
{
	/* Link to previous TSS */
	uint32_t	prev_tss;

	/* Ring 0 */
	uint32_t	esp0;
	uint32_t	ss0;

	/* Ring 1 */
	uint32_t	esp1;
	uint32_t	ss1;

	/* Ring 2 */
	uint32_t	esp2;
	uint32_t	ss2;

	/* Paging */
	uint32_t	cr3;

	/* State */
	uint32_t	eip, eflags;

	/* Registers */
	uint32_t	eax, ecx, edx, ebx;
	uint32_t	esp, ebp, esi, edi;

	/* Segments */
	uint32_t	es;
	uint32_t	cs;
	uint32_t	ss;
	uint32_t	ds;
	uint32_t	fs;
	uint32_t	gs;

	/* Misc */
	uint32_t	ldt;
	uint16_t	trap;
	uint16_t	io_map;
} tss_entry_t;
#pragma pack(pop)

/* GDT Prototypes */
_CRT_EXTERN void gdt_init(void);
_CRT_EXTERN void gdt_install_descriptor(uint32_t base, uint32_t limit,
										uint8_t access, uint8_t grandularity);


/* TSS Prototypes */
_CRT_EXTERN void tss_install(uint32_t gdt_index);
_CRT_EXTERN void gdt_update_tss(uint32_t cpu, uint32_t stack);

/* Should be called by AP cores */
_CRT_EXTERN void gdt_install(void);
_CRT_EXTERN void gdt_install_tss(void);

#endif // !_x86_GDT_H_

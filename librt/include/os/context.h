/* MollenOS
 *
 * Copyright 2011 - 2017, Philip Meulengracht
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
 * MollenOS MCore - Context Definitions & Structures
 * - This header describes the base context-structure, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#ifndef __CONTEXT_INTERFACE_H__
#define __CONTEXT_INTERFACE_H__

/* Includes
 * - Library */
#include <os/osdefs.h>

/* The context depends on the current running 
 * architecture - and describes which kind of
 * information is stored for each context */
#if defined(_X86_32) || defined(i386)
PACKED_TYPESTRUCT(Context, {
	uint32_t                Edi;
	uint32_t                Esi;
	uint32_t                Ebp;
	uint32_t                Esp;
	uint32_t                Ebx;
	uint32_t                Edx;
	uint32_t                Ecx;
	uint32_t                Eax;
			                
	uint32_t                Gs;
	uint32_t                Fs;
	uint32_t                Es;
	uint32_t                Ds;
			                
	uint32_t                Irq;
	uint32_t                ErrorCode;
	uint32_t                Eip;
	uint32_t                Cs;
	uint32_t                Eflags;
			                
	uint32_t                UserEsp;
	uint32_t                UserSs;
	uint32_t                UserArg;
			                
	uint32_t                Reserved[4];
});
#else
#error "os/context.h: Invalid architecture"
#endif

#endif //!__CONTEXT_INTERFACE_H__

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
#if defined(__i386__) || defined(i386)
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
    
    uint32_t                Arguments[6];
});
#define CONTEXT_IP(Context)     Context->Eip
#elif defined(__amd64__) || defined(amd64)
PACKED_TYPESTRUCT(Context, {
	uint64_t                Rdi;
	uint64_t                Rsi;
	uint64_t                Rbp;
	uint64_t                Rsp;
	uint64_t                Rbx;
	uint64_t                Rdx;
	uint64_t                Rcx;
	uint64_t                Rax;
	
    uint64_t                R8;
	uint64_t                R9;
	uint64_t                R10;
	uint64_t                R11;
	uint64_t                R12;
	uint64_t                R13;
	uint64_t                R14;
	uint64_t                R15;
			                
	uint64_t                Gs;
	uint64_t                Fs;
	uint64_t                Es;
	uint64_t                Ds;
			                
	uint64_t                Irq;
	uint64_t                ErrorCode;
	uint64_t                Rip;
	uint64_t                Cs;
	uint64_t                Rflags;
			                
	uint64_t                UserRsp;
	uint64_t                UserSs;
    
    uint64_t                Arguments[6];
});
#define CONTEXT_IP(Context)     Context->Rip
#else
#error "os/context.h: Invalid architecture"
#endif

#endif //!__CONTEXT_INTERFACE_H__

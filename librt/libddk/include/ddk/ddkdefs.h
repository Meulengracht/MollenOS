/**
 * MollenOS
 *
 * Copyright 2019, Philip Meulengracht
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
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * DDK Definitions & Structures
 * - This header describes the base ddk-structure, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#ifndef __DDK_DEFINITIONS_H__
#define __DDK_DEFINITIONS_H__

#include <os/mollenos.h>

#define DDKDECL(ReturnType, Function) extern ReturnType Function
#define DDKDECL_DATA(Type, Name) extern Type Name

#if defined(i386) || defined(__i386__)
#define TLS_VALUE   uint32_t
#if (defined (__GNUC__))
// TODO
#define TLS_READ(offset, value)  (void)offset
#define TLS_WRITE(offset, value) (void)offset
#else
#define TLS_READ(offset, value)  __asm { __asm mov ebx, [offset] __asm mov eax, gs:[ebx] __asm mov [value], eax }
#define TLS_WRITE(offset, value) __asm { __asm mov ebx, [offset] __asm mov eax, [value] __asm mov gs:[ebx], eax }
#endif
#elif defined(amd64) || defined(__amd64__)
#define TLS_VALUE   uint64_t
#if (defined (__GNUC__))
// TODO
#define TLS_READ(offset, value)  (void)offset
#define TLS_WRITE(offset, value) (void)offset
#else
#define TLS_READ(offset, value)  __asm { __asm mov rbx, [offset] __asm mov rax, gs:[rbx] __asm mov [value], rax }
#define TLS_WRITE(offset, value) __asm { __asm mov rbx, [offset] __asm mov rax, [value] __asm mov gs:[rbx], rax }
#endif
#else
#error "Implement rw for tls for this architecture"
#endif

/**
 * Introduce interrupt status codes for drivers, these are used in the fast interrupt handlers.
 */
typedef enum {
    IRQSTATUS_NOT_HANDLED,
    IRQSTATUS_HANDLED
} irqstatus_t;

/**
 * @brief Read from the TLS register index. On the X86 architecture this is done by using
 * the GS register.
 */
SERVICEAPI size_t SERVICEABI
__get_reserved(
        _In_ size_t tlsIndex)
{
    TLS_VALUE tlsValue  = 0;
    size_t    tlsOffset = (tlsIndex * sizeof(TLS_VALUE));
    TLS_READ(tlsOffset, tlsValue);
    return (size_t)tlsValue;
}

/**
 * @brief Write to the TLS register index. On the X86 architecture this is done by using
 * the GS register.
 */
SERVICEAPI void SERVICEABI
__set_reserved(
        _In_ size_t    tlsIndex,
        _In_ TLS_VALUE tlsValue)
{
    size_t tlsOffset = (tlsIndex * sizeof(TLS_VALUE));
    TLS_WRITE(tlsOffset, tlsValue);
}

#endif //!__DDK_DEFINITIONS_H__

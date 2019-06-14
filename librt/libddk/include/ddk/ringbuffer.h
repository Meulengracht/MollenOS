/* MollenOS
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
 * along with this program.If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * Ringbuffer Type Definitions & Structures
 * - This header describes the base ringbuffer-structure, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#ifndef __RINGBUFFER_H__
#define __RINGBUFFER_H__

#include <os/osdefs.h>
#include <ddk/ddkdefs.h>

#define RINGBUFFER_NO_WRITE_BLOCK       0x1
#define RINGBUFFER_NO_READ_BLOCK        0x2
#define RINGBUFFER_MULTIPLE_READERS     0x4
#define RINGBUFFER_MULTIPLE_WRITERS     0x8
#define RINGBUFFER_ALLOW_PARTIAL_READS  0x10
#define RINGBUFFER_ALLOW_PARTIAL_WRITES 0x20
#define RINGBUFFER_GLOBAL               0x40

typedef struct {
    size_t       capacity;
    uint8_t*     buffer;
    unsigned int options;
    
    _Atomic(int)          consumer_count;
    _Atomic(unsigned int) consumer_index;
    _Atomic(unsigned int) consumer_comitted_index;
    _Atomic(int)          producer_count;
    _Atomic(unsigned int) producer_index;
    _Atomic(unsigned int) producer_comitted_index;
} ringbuffer_t;

DDKDECL(void,
ringbuffer_construct(
    _In_ ringbuffer_t* ring,
    _In_ size_t        capacity,
    _In_ uint8_t*      buffer,
    _In_ unsigned int  options));

DDKDECL(OsStatus_t,
ringbuffer_create(
    _In_  size_t         capacity,
    _In_  unsigned int   options,
    _Out_ ringbuffer_t** ring_out));

DDKDECL(void,
ringbuffer_set_option(
    _In_ ringbuffer_t* ring,
    _In_ unsigned int  option));

DDKDECL(void,
ringbuffer_clear_option(
    _In_ ringbuffer_t* ring,
    _In_ unsigned int  option));

DDKDECL(size_t,
ringbuffer_write(
    _In_ ringbuffer_t* ring,
    _In_ const char*   buffer,
    _In_ size_t        length));

DDKDECL(size_t,
ringbuffer_read(
    _In_ ringbuffer_t* ring,
    _In_ char*         buffer,
    _In_ size_t        length));

#endif //!__RINGBUFFER_H__

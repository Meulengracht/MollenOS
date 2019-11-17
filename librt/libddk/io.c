/**
 * MollenOS
 *
 * Copyright 2017, Philip Meulengracht
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
 * I/O Definitions & Structures
 * - This header describes the base io-structure, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#include <ddk/io.h>

void
ReadVolatileMemory(
    _In_ const volatile void* Pointer,
    _In_ volatile void*       Out,
    _In_ size_t               Length)
{
    if (Length == 1) {
        *(uint8_t*)Out = *(volatile uint8_t*)Pointer;
    }
    else if (Length == 2) {
        *(uint16_t*)Out = *(volatile uint16_t*)Pointer;
    }
    else if (Length == 4) {
        *(uint32_t*)Out = *(volatile uint32_t*)Pointer;
    }
    else if (Length == 8) {
        *(uint64_t*)Out = *(volatile uint64_t*)Pointer;
    }
    else {
        sw_mb();
        memcpy((void*)Out, (const void*)Pointer, Length);
        sw_mb();
    }
}

void
WriteVolatileMemory(
    _In_ volatile void* Pointer,
    _In_ void*          Data,
    _In_ size_t         Length)
{
    if (Length == 1) {
        *((volatile uint8_t*)Pointer) = *(uint8_t*)Data;
    }
    else if (Length == 2) {
        *((volatile uint16_t*)Pointer) = *(uint16_t*)Data;
    }
    else if (Length == 4) {
        *((volatile uint32_t*)Pointer) = *(uint32_t*)Data;
    }
    else if (Length == 8) {
        *((volatile uint64_t*)Pointer) = *(uint64_t*)Data;
    }
    else {
        sw_mb();
        memcpy((void*)Pointer, (const void*)Data, Length);
        sw_mb();
    }
}

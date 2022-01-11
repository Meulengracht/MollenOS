/**
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
 * Platform Interface
 * - Contains platform specific things and includes relevant platform headers
 */

#ifndef __PLATFORM_INTEFACE_H__
#define __PLATFORM_INTEFACE_H__

#include <os/osdefs.h>

// A few macros and structures must be defined by the platform
// Structures
// PlatformCpuBlock_t     - Data that is Processor specific
// PlatformCpuCoreBlock_t - Data that is Core specific
// PlatformThreadBlock_t  - Data that is specific to the thread
// PlatformMemoryBlock_t  - Data that is specific to the memory space
//
// Macros
// __PLATFORM_READTLS(offset)
// __PLATFORM_WRITETLS(offset, value)
//

#if defined(__i386__) || defined(__amd64__)
#include <arch/x86/arch.h>
#else
#error "platform.h: Current VALI_ARCH is unrecognized/unimplemented"
#endif

#endif //!__PLATFORM_INTEFACE_H__

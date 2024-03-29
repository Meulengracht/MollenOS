/**
 * Copyright 2021, Philip Meulengracht
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
 */

#ifndef __LOADER_H__
#define __LOADER_H__

#include <Uefi.h>
#include <GlobalTable.h>
#include <vboot/vboot.h>

#if defined(__i386__) || defined(__amd64__)
#define LOADER_KERNEL_BASE       0x100000
#define LOADER_KERNEL_STACK_SIZE 0x10000
#else
#error "Unsupported architecture"
#endif

EFI_STATUS LoaderInitialize(void);

EFI_STATUS LoadResources(
    IN  struct VBoot* VBoot,
    OUT VOID**        KernelStack);

#endif //!__LOADER_H__

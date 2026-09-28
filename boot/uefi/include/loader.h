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
#include <vboot/vboot.h>

#if defined(__i386__) || defined(__amd64__)
#define LOADER_KERNEL_BASE       0x100000
#define LOADER_KERNEL_STACK_SIZE 0x10000
#elif defined(__aarch64__)
/**
 * The bootstrap kernel is reserved with `AllocatePages(AllocateAddress,
 * EfiLoaderCode, ...)` at `0x48000000`, inside platform RAM, before copying PE
 * sections. Images are bounded to 64 MiB; allocation failure is fatal, with no
 * fallback to `0x100000` or an arbitrary address. ARM64 PE32+ is required; TE and
 * foreign machine types are rejected. The root kernel linker selects this base
 * for ARM64 and keeps the existing x86 base independent.
 *
 * Initial linked virtual base is also `0x48000000`, by explicit bootstrap policy.
 * The PE loader executes its normal base-relocation pass with zero delta. A kernel
 * linked at another base is rejected rather than silently relocating a higher-half
 * kernel for physical execution. Future higher-half images need a position-independent
 * physical entry stub and kernel-owned mappings before branching to linked code.
 * ARM64 COFF linking keeps relocation support (LLD forbids `/fixed` for ARM64).
 * Phoenix preserves its preferred linked address; its sections are staged elsewhere
 * without rebasing, for the kernel to map later.
 */
#define LOADER_KERNEL_BASE       0x48000000ULL
#define LOADER_KERNEL_STACK_SIZE 0x10000
#else
#error "Unsupported architecture"
#endif

EFI_STATUS LoaderInitialize(void);

EFI_STATUS LoadResources(
    IN  struct VBoot* VBoot,
    OUT VOID**        KernelStack);

#endif //!__LOADER_H__

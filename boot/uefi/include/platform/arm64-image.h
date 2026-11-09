/**
 * Copyright, Philip Meulengracht
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

#ifndef __ARM64_IMAGE_H__
#define __ARM64_IMAGE_H__

#include <Uefi.h>
#include <vboot/vboot.h>

// Keep the kernel inside the initial TTBR0 identity map. This limit does not
// restrict the ARM64 physical address size.
#define ARM64_KERNEL_LIMIT    0x8000000000ULL
#define ARM64_KERNEL_MAX_SIZE 0x04000000U

/**
 * @brief Loads an ARM64 PE kernel for physical execution with the MMU off.
 * @param Buffer Decompressed PE file. The routine leaves it unchanged.
 * @param Size Number of readable bytes in Buffer.
 * @param Kernel Receives the reserved physical image and physical entry on success.
 * @return EFI_SUCCESS or an image/allocation error. Failed loads free the image allocation.
 */
EFI_STATUS
Arm64LoadKernelImage(
    IN const VOID* Buffer,
    IN UINTN Size,
    OUT struct VBootModule* Kernel);

#endif //!__ARM64_IMAGE_H__

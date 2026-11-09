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

#ifndef __PLATFORM_H__
#define __PLATFORM_H__

#include <Uefi.h>
#include <Library/UefiBootServicesTableLib.h>
#include <vboot/vboot.h>

EFI_STATUS LibraryInitialize(
    IN EFI_HANDLE        ImageHandle,
    IN EFI_SYSTEM_TABLE* SystemTable);

/**
 * @brief Publishes the final memory map and exits boot services.
 *
 * @param VBoot Descriptor initialized by the loader.
 * @return EFI_SUCCESS after exit, or an error before exit begins. Errors in
 *         the final-map loop stop execution.
 */
EFI_STATUS
LibraryCleanup(
    IN struct VBoot* VBoot);

EFI_STATUS LibraryAllocateMemory(
    IN UINTN   Size,
    OUT VOID** Memory);

EFI_STATUS LibraryFreeMemory(
    IN VOID* Memory);

/**
 * @brief Checks CPU features required by the physical handoff.
 *
 * @return EFI_SUCCESS for supported EL1/EL2 state, otherwise EFI_UNSUPPORTED.
 *         Leaves firmware translation and exception controls unchanged.
 */
EFI_STATUS
Arm64CheckCpu(
    VOID);

/**
 * @brief Checks CPU state and copies the EFI configuration table list.
 *
 * @param Boot Descriptor with the loaded kernel, stack, ramdisk, and Phoenix ranges.
 * @return EFI_SUCCESS when the physical handoff can proceed.
 */
EFI_STATUS
Arm64Prepare(
    IN struct VBoot* Boot);

BOOLEAN Arm64Identity(
    IN UINT64 Base,
    IN UINT64 Length);

/**
 * @brief Switches to EL1h and branches to the physical kernel entry.
 *
 * @param Boot Physical descriptor checked by Arm64Prepare.
 * @param Entry Physical kernel entry address.
 * @param Stack Aligned exclusive top of the private stack.
 * @return Does not return. Boot services must have exited.
 */
VOID
Arm64Enter(
    IN VOID* Boot,
    IN UINT64 Entry,
    IN VOID* Stack) __attribute__((noreturn));

#endif //!__PLATFORM_H__

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

#ifndef __PLATFORM_H__
#define __PLATFORM_H__

#include <Uefi.h>
#include <Library/UefiBootServicesTableLib.h>
#include <vboot/vboot.h>

EFI_STATUS LibraryInitialize(
    IN EFI_HANDLE        ImageHandle,
    IN EFI_SYSTEM_TABLE* SystemTable);

EFI_STATUS LibraryCleanup(
    IN struct VBoot* VBoot);

EFI_STATUS LibraryAllocateMemory(
    IN UINTN   Size,
    OUT VOID** Memory);

EFI_STATUS LibraryFreeMemory(
    IN VOID* Memory);

EFI_STATUS Arm64Prepare(
    IN struct VBoot* Boot);

BOOLEAN Arm64Identity(
    IN UINT64 Base,
    IN UINT64 Length);

VOID Arm64Enter(
    IN VOID* Boot,
    IN UINT64 Entry,
    IN VOID* Stack) __attribute__((noreturn));

#endif //!__PLATFORM_H__

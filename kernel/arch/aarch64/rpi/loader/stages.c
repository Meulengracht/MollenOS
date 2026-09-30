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

#include "loader.h"

enum RpiBootStatus
RpiPlatformPrepare(
    struct RpiBootContext* context)
{
    (void)context;
    // Success without reservations could authorize overwriting resident firmware.
    // TODO: generic FDT discovery, reservation tracking, and board UART setup.
    return RpiBootUnsupported;
}

enum RpiBootStatus
RpiLoadKernel(
    struct RpiBootContext* context)
{
    (void)context;
    // Share the EFI loader's PE core through allocation/read callbacks before
    // using it here: this target cannot depend on EDK2 or EFI boot services.
    // TODO: validated sections, disjoint destination, zero-fill, and relocation.
    return RpiBootUnsupported;
}

enum RpiBootStatus
RpiBuildContract(
    struct RpiBootContext* context)
{
    (void)context;
    // A DTB is not an EFI configuration array. The shared ABI first needs an
    // explicit native/FDT description, rather than repurposing UEFI fields.
    // TODO: versioned FDT ownership and separate initrd/Phoenix input.
    return RpiBootUnsupported;
}

_Noreturn void
RpiTransferToKernel(
    struct RpiBootContext* context)
{
    (void)context;
    // Stack and EL changes require a no-return assembly boundary after loading.
    // A C function-pointer cast cannot establish that entry contract.
    // TODO: cache synchronization, EL1 normalization, vectors, and x0 = VBoot.
    RpiLoaderStop(RpiBootUnsupported);
}
